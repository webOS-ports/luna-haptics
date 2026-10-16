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

#ifndef FAKEBUS_H
#define FAKEBUS_H

#include <map>
#include <string>
#include <vector>

#include <luna-service2/lunaservice.h>

/**
 * luna-service2, in process. The test executable defines the LS* functions
 * the daemon uses and is not linked with the real library, so these are the
 * only ones there are.
 *
 * Every failure it reports carries an LSError whose message is heap
 * allocated, as the real library's are, so a missing LSErrorFree() shows up
 * as a leak under ASan and valgrind.
 *
 * Messages are heap allocated per call and freed when the call returns - or,
 * for a call the daemon subscribed, when the subscription ends, as
 * luna-service2 keeps a subscribed message alive until then. Holding on to
 * a message's token past that is a use after free ASan sees.
 */
namespace fakebus {

//! Clears everything recorded and every failure armed. Subscriptions still
//! open are dropped without telling the daemon.
void reset();

//! Make the next call to the named function fail ("LSRegister",
//! "LSRegisterCategory", "LSCategorySetData",
//! "LSSubscriptionSetCancelFunction", "LSGmainAttach", "LSUnregister",
//! "LSMessageReply", "LSSubscriptionAdd").
void failNext(const std::string& function);

//! A caller. Its unique token is "<sender>.<serial>", as luna-service2's is.
struct Caller {
    std::string sender = ":1.100";
    //! false: the caller went away before the daemon got to the call;
    //! LSSubscriptionAdd() then fails, as the real one does.
    bool connected = true;
};

//! Calls the method the daemon registered under category "/" as caller
//! would, with payload as the message's payload (NULL for none). Returns
//! what the handler returned; the replies land in replies(). The message's
//! unique token is in lastToken().
bool callMethod(const std::string& method, const char* payload, const Caller& caller = Caller());

//! The unique token of the last message callMethod() sent.
const std::string& lastToken();

//! Every reply sent, in order, since reset().
const std::vector<std::string>& replies();

//! LSCallCancel() from the caller of the call with this token: the
//! daemon's cancel function runs for it if it is subscribed. False if it
//! was not.
bool cancel(const std::string& token);

//! The client sender disconnects: every subscription it holds ends, newest
//! first, as luna-service2 ends them. Returns how many.
int disconnect(const std::string& sender);

//! Unique tokens of the subscriptions open, oldest first.
std::vector<std::string> subscriptions();

//! Methods registered per category, in table order.
const std::map<std::string, std::vector<std::string> >& categories();

//! The name the daemon registered under, empty if none.
const std::string& registeredName();

//! Calls made to each LS* function since reset().
int calls(const std::string& function);

//! LSError messages handed out and not yet freed with LSErrorFree().
int errorsOutstanding();

//! Handles registered and not yet unregistered.
int handlesOutstanding();

} // namespace fakebus

#endif // FAKEBUS_H
