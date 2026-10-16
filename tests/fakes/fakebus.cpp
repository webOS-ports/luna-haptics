/* @@@LICENSE
*
*      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#include "fakebus.h"

#include <glib.h>

#include <stdio.h>

#include <set>

struct LSHandle {
    std::string name;
    std::map<std::string, std::vector<LSMethod> > categories;
    std::map<std::string, void*> categoryData;
    LSFilterFunc cancelFunction = nullptr;
    void* cancelContext = nullptr;
};

struct LSMessage {
    const char* payload;
    // Heap allocated, so a token pointer kept past the message's life is
    // a use after free the sanitizers see.
    char* token;
    std::string sender;
    bool connected;
};

namespace {

std::set<std::string> s_failNext;
std::vector<std::string> s_replies;
std::map<std::string, std::vector<std::string> > s_categories;
std::map<std::string, int> s_calls;
std::string s_name;
std::string s_lastToken;
LSHandle* s_handle = nullptr;
int s_errors = 0;
int s_handles = 0;
int s_serial = 0;
// Oldest first; each owns its message, as luna-service2 holds a reference
// to a subscribed message until the subscription ends.
std::vector<LSMessage*> s_subscriptions;

// Records the call; true if the test armed a failure for it.
bool enter(const char* function, LSError* error, const char* why = "failed as asked")
{
    ++s_calls[function];
    if (!s_failNext.erase(function))
        return false;

    if (error) {
        error->error_code = -1;
        error->message = g_strdup_printf("fakebus: %s %s", function, why);
        error->file = __FILE__;
        error->line = __LINE__;
        error->func = function;
        ++s_errors;
    }
    return true;
}

void freeMessage(LSMessage* message)
{
    g_free(message->token);
    delete message;
}

// Ends the subscription at index i: the daemon's cancel function first,
// then the message is dropped, as _CatalogRemoveToken() does.
void endSubscription(size_t i)
{
    LSMessage* message = s_subscriptions[i];
    s_subscriptions.erase(s_subscriptions.begin() + i);
    if (s_handle && s_handle->cancelFunction)
        s_handle->cancelFunction(s_handle, message, s_handle->cancelContext);
    freeMessage(message);
}

} // namespace

namespace fakebus {

void reset()
{
    s_failNext.clear();
    s_replies.clear();
    s_categories.clear();
    s_calls.clear();
    s_name.clear();
    s_lastToken.clear();
    s_errors = 0;
    for (LSMessage* m : s_subscriptions)
        freeMessage(m);
    s_subscriptions.clear();
    // A handle the daemon leaked stays leaked, so the leak checkers see it;
    // s_handles keeps counting it.
    s_handle = nullptr;
}

void failNext(const std::string& function)
{
    s_failNext.insert(function);
}

bool callMethod(const std::string& method, const char* payload, const Caller& caller)
{
    if (!s_handle)
        return false;

    std::map<std::string, std::vector<LSMethod> >::const_iterator c = s_handle->categories.find("/");
    if (c == s_handle->categories.end())
        return false;

    for (const LSMethod& m : c->second) {
        if (method != m.name)
            continue;

        LSMessage* message = new LSMessage();
        message->payload = payload;
        message->token = g_strdup_printf("%s.%d", caller.sender.c_str(), ++s_serial);
        message->sender = caller.sender;
        message->connected = caller.connected;
        s_lastToken = message->token;

        bool result = m.function(s_handle, message, s_handle->categoryData["/"]);

        bool subscribed = false;
        for (LSMessage* s : s_subscriptions)
            subscribed = subscribed || s == message;
        if (!subscribed)
            freeMessage(message);
        return result;
    }
    return false;
}

const std::string& lastToken() { return s_lastToken; }

const std::vector<std::string>& replies() { return s_replies; }

bool cancel(const std::string& token)
{
    for (size_t i = 0; i < s_subscriptions.size(); ++i) {
        if (token == s_subscriptions[i]->token) {
            endSubscription(i);
            return true;
        }
    }
    return false;
}

int disconnect(const std::string& sender)
{
    int n = 0;
    for (size_t i = s_subscriptions.size(); i-- > 0;) {
        if (s_subscriptions[i]->sender == sender) {
            endSubscription(i);
            ++n;
        }
    }
    return n;
}

std::vector<std::string> subscriptions()
{
    std::vector<std::string> tokens;
    for (LSMessage* m : s_subscriptions)
        tokens.push_back(m->token);
    return tokens;
}

const std::map<std::string, std::vector<std::string> >& categories() { return s_categories; }
const std::string& registeredName() { return s_name; }
int calls(const std::string& function) { return s_calls[function]; }
int errorsOutstanding() { return s_errors; }
int handlesOutstanding() { return s_handles; }

} // namespace fakebus

bool LSErrorInit(LSError* error)
{
    error->error_code = 0;
    error->message = nullptr;
    error->file = nullptr;
    error->line = 0;
    error->func = nullptr;
    error->padding = nullptr;
    error->magic = 0;
    return true;
}

void LSErrorFree(LSError* error)
{
    if (error && error->message) {
        g_free(error->message);
        error->message = nullptr;
        --s_errors;
    }
}

void LSErrorPrint(LSError* error, FILE* out)
{
    if (error && error->message)
        fprintf(out, "LSError: %s\n", error->message);
}

bool LSRegister(const char* name, LSHandle** sh, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;

    *sh = new LSHandle();
    (*sh)->name = name;
    s_name = name;
    s_handle = *sh;
    ++s_handles;
    return true;
}

bool LSRegisterCategory(LSHandle* sh, const char* category, LSMethod* methods,
                        LSSignal* /*langis*/, LSProperty* /*properties*/, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;

    std::vector<LSMethod>& table = sh->categories[category];
    std::vector<std::string>& names = s_categories[category];
    for (LSMethod* m = methods; m && m->name; ++m) {
        table.push_back(*m);
        names.push_back(m->name);
    }
    return true;
}

bool LSCategorySetData(LSHandle* sh, const char* category, void* user_data, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;

    sh->categoryData[category] = user_data;
    return true;
}

bool LSSubscriptionSetCancelFunction(LSHandle* sh, LSFilterFunc cancelFunction, void* ctx, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;

    sh->cancelFunction = cancelFunction;
    sh->cancelContext = ctx;
    return true;
}

bool LSGmainAttach(LSHandle* /*sh*/, GMainLoop* /*mainLoop*/, LSError* lserror)
{
    return !enter(__func__, lserror);
}

bool LSUnregister(LSHandle* service, LSError* lserror)
{
    // The real library frees the handle whatever it reports, and its
    // subscriptions with it - without calling the cancel function.
    if (s_handle == service) {
        s_handle = nullptr;
        for (LSMessage* m : s_subscriptions)
            freeMessage(m);
        s_subscriptions.clear();
    }
    delete service;
    --s_handles;
    return !enter(__func__, lserror);
}

const char* LSMessageGetPayload(LSMessage* message)
{
    ++s_calls[__func__];
    return message->payload;
}

const char* LSMessageGetUniqueToken(LSMessage* message)
{
    ++s_calls[__func__];
    return message->token;
}

bool LSSubscriptionAdd(LSHandle* /*sh*/, const char* /*key*/, LSMessage* message, LSError* lserror)
{
    if (!message->connected) {
        // As _CatalogAdd() refuses it.
        s_failNext.insert(__func__);
        enter(__func__, lserror, "The client has been disconnected");
        return false;
    }
    if (enter(__func__, lserror))
        return false;

    for (LSMessage* s : s_subscriptions) {
        if (s == message)
            return true;
    }
    s_subscriptions.push_back(message);
    return true;
}

bool LSMessageReply(LSHandle* /*sh*/, LSMessage* /*lsmsg*/, const char* replyPayload, LSError* lserror)
{
    // Kept even when the send "fails": the test wants to see what was tried.
    s_replies.push_back(replyPayload ? replyPayload : "(null)");
    return !enter(__func__, lserror);
}
