/* @@@LICENSE
*
*      Copyright (c) 2008-2013 LG Electronics, Inc.
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

#include "HapticsController.h"
#include "NyxHapticsController.h"

#include <json.h>
#include <glib.h>
#include <luna-service2/lunaservice.h>
#include <stdint.h>
#include <string.h>

HapticsController* HapticsController::s_instance = NULL;

static const char *const s_replyOk = "{\"returnValue\":true}";
static const char *const s_replyInvalid = "{\"returnValue\":false,\"errorText\":\"Invalid arguments\"}";
static const char *const s_replyFailed = "{\"returnValue\":false,\"errorText\":\"Unable to vibrate\"}";

HapticsController::HapticsController()
    : m_service(NULL)
{
}

HapticsController::~HapticsController()
{
    if (m_service) {
        LSError lsError;
        LSErrorInit(&lsError);
        if (!LSUnregister(m_service, &lsError)) {
            g_warning("Failed to unregister com.palm.vibrate: %s", lsError.message);
            LSErrorFree(&lsError);
        }
        m_service = NULL;
    }
}

HapticsController* HapticsController::instance()
{
    if (!s_instance)
        s_instance = new NyxHapticsController();

    return s_instance;
}

void HapticsController::destroy()
{
    delete s_instance;
    s_instance = NULL;
}

void HapticsController::addSubscription(const char *token, int id)
{
    Subscription s;
    s.token = token;
    s.id = id;
    m_subscriptions.push_back(s);
}

void HapticsController::endSubscription(const char *token)
{
    if (!token)
        return;

    for (std::vector<Subscription>::iterator it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        if (it->token != token)
            continue;

        int id = it->id;
        bool newest = (it + 1 == m_subscriptions.end());
        m_subscriptions.erase(it);
        if (newest)
            cancel(id);
        return;
    }
}

// The typed members of a request. A member that is not there (or is JSON
// null) leaves *out alone and is fine unless required; one that is there has
// to have the type asked for. json-c would convert anything on request: "abc"
// reads as 0, the string "false" as true, {} as the string "{ }".

// A number from 0 to INT32_MAX; larger ones are clamped, fractions dropped.
static bool getNonNegativeInt(json_object *root, const char *key, bool required, int *out)
{
    json_object *value = NULL;
    if (!json_object_object_get_ex(root, key, &value) || !value)
        return !required;

    if (json_object_is_type(value, json_type_int)) {
        // Clamped to the int32_t range by json-c.
        int32_t n = json_object_get_int(value);
        if (n < 0)
            return false;
        *out = n;
        return true;
    }

    if (json_object_is_type(value, json_type_double)) {
        // Not json_object_get_int(): it casts NaN, which json-c parses, to
        // int32_t, and that is undefined. NaN fails the first test here.
        double d = json_object_get_double(value);
        if (!(d >= 0))
            return false;
        *out = d >= INT32_MAX ? INT32_MAX : static_cast<int>(d);
        return true;
    }

    return false;
}

static bool getString(json_object *root, const char *key, bool required, const char **out)
{
    json_object *value = NULL;
    if (!json_object_object_get_ex(root, key, &value) || !value)
        return !required;

    if (!json_object_is_type(value, json_type_string))
        return false;
    *out = json_object_get_string(value);
    return true;
}

static bool getBool(json_object *root, const char *key, bool required, bool *out)
{
    json_object *value = NULL;
    if (!json_object_object_get_ex(root, key, &value) || !value)
        return !required;

    if (!json_object_is_type(value, json_type_boolean))
        return false;
    *out = json_object_get_boolean(value);
    return true;
}

// Ties an infinite effect to the call's subscription. If that cannot be
// done - the client has already gone, for one - nothing could ever cancel
// the effect, so it is stopped here and the call fails.
static bool subscribe(HapticsController *hc, LSHandle *lh, LSMessage *m, int id)
{
    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSSubscriptionAdd(lh, "com.palm.vibrate/vibrate", m, &lsError)) {
        g_warning("Failed to tie an infinite vibration to its caller, cancelling it: %s", lsError.message);
        LSErrorFree(&lsError);
        hc->cancel(id);
        return false;
    }

    hc->addSubscription(LSMessageGetUniqueToken(m), id);
    return true;
}

static void reply(LSHandle *lh, LSMessage *m, const char *payload)
{
    LSError lsError;
    LSErrorInit(&lsError);

    if (!LSMessageReply(lh, m, payload, &lsError)) {
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }
}

/*!
\page com_palm_vibrate Service API com.palm.vibrate/
\section com_palm_vibrate_vibrate vibrate

com.palm.vibrate/vibrate

Vibrate with a given period, either for a set duration or until cancelled.

\subsection syntax Syntax:
\code
{
    "period": integer,
    "duration": integer
}
\endcode

\param period Period of the vibration in ms, 0 or more. \e Required.
\param duration Duration in ms, 0 or more. Leave out (or 0) to vibrate until
       the call is cancelled with LSCallCancel, or the caller goes away.

\subsection example Example:
\code
luna-send -n 1 -f luna://com.palm.vibrate/vibrate '{ "period": 500, "duration": 4000 }'
\endcode
*/
static bool cbVibrate(LSHandle *lh, LSMessage *m, void *ctx)
{
    HapticsController *hc = static_cast<HapticsController *>(ctx);
    const char *str = LSMessageGetPayload(m);
    struct json_object *root = str ? json_tokener_parse(str) : NULL;
    int period = 0;
    int duration = 0; // infinity
    const char *result = s_replyInvalid;

    // Negative values mean nothing, and the nyx modules disagree on what to
    // do with them: hybris refuses them, timedoutput plays a negative period
    // as one buzz of the duration. Refused here, alike for every module.
    if (!root || !json_object_is_type(root, json_type_object)
        || !getNonNegativeInt(root, "period", true, &period)
        || !getNonNegativeInt(root, "duration", false, &duration))
        goto done;

    {
        int id = hc->vibrate(period, duration);
        bool ok = id >= 0 && (duration != 0 || subscribe(hc, lh, m, id));
        result = ok ? s_replyOk : s_replyFailed;
    }

done:
    if (root)
        json_object_put(root);
    reply(lh, m, result);
    return true;
}

/*!
\page com_palm_vibrate
\section com_palm_vibrate_vibrate_named_effect vibrateNamedEffect

com.palm.vibrate/vibrateNamedEffect

Play a named vibration effect: ringtone, alert, notification, tapdown, tapup.

\subsection syntax Syntax:
\code
{
    "name": string,
    "continous": boolean
}
\endcode

\param name Name of the effect. \e Required.
\param continous If true, the effect is tied to the call: cancelling it with
       LSCallCancel, or the caller going away, stops the effect.
*/
static bool cbVibrateNamedEffect(LSHandle *lh, LSMessage *m, void *ctx)
{
    HapticsController *hc = static_cast<HapticsController *>(ctx);
    const char *str = LSMessageGetPayload(m);
    struct json_object *root = str ? json_tokener_parse(str) : NULL;
    const char *name = NULL;
    bool continous = false;
    const char *result = s_replyInvalid;

    // "continous" is the protocol's spelling.
    if (!root || !json_object_is_type(root, json_type_object)
        || !getString(root, "name", true, &name)
        || !getBool(root, "continous", false, &continous))
        goto done;

    {
        int id = hc->vibrate(name);
        bool ok = id >= 0 && (!continous || subscribe(hc, lh, m, id));
        result = ok ? s_replyOk : s_replyFailed;
    }

done:
    if (root)
        json_object_put(root);
    reply(lh, m, result);
    return true;
}

static bool cbCancelSubscription(LSHandle * /*lh*/, LSMessage *message, void *ctx)
{
    HapticsController *hc = static_cast<HapticsController *>(ctx);
    hc->endSubscription(LSMessageGetUniqueToken(message));
    return true;
}

static LSMethod s_methods[]  = {
    { "vibrate",            cbVibrate,            LUNA_METHOD_FLAGS_NONE },
    { "vibrateNamedEffect", cbVibrateNamedEffect, LUNA_METHOD_FLAGS_NONE },
    { 0, 0, LUNA_METHOD_FLAGS_NONE },
};

bool HapticsController::startService(GMainLoop* mainLoop)
{
    LSError lsError;
    LSErrorInit(&lsError);

    // A failure part way leaves m_service set; destroy() unregisters it.
    bool result = LSRegister("com.palm.vibrate", &m_service, &lsError)
        && LSRegisterCategory(m_service, "/", s_methods, NULL, NULL, &lsError)
        && LSCategorySetData(m_service, "/", this, &lsError)
        && LSSubscriptionSetCancelFunction(m_service, cbCancelSubscription, this, &lsError)
        && LSGmainAttach(m_service, mainLoop, &lsError);

    if (!result) {
        g_warning("Failed to start com.palm.vibrate service: %s", lsError.message);
        LSErrorFree(&lsError);
    } else {
        g_debug("com.palm.vibrate service started");
    }
    return result;
}
