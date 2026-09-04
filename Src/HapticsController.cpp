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
#include <string.h>

HapticsController* HapticsController::s_instance = NULL;

HapticsController::HapticsController()
    : m_service(NULL)
{
    // Keys are strdup'ed copies of LS2 message tokens: the token string
    // belongs to its message, and an entry has to survive the message that
    // created it (it is looked up again from the cancel callback).
    m_mappingTable = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

HapticsController* HapticsController::instance()
{
    if (!s_instance)
        s_instance = new NyxHapticsController();

    return s_instance;
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

\param period Period of the vibration in ms. \e Required.
\param duration Duration in ms. Leave out (or 0) to vibrate until the call is
       cancelled with LSCallCancel.

\subsection example Example:
\code
luna-send -n 1 -f luna://com.palm.vibrate/vibrate '{ "period": 500, "duration": 4000 }'
\endcode
*/
static bool cbVibrate(LSHandle *lh, LSMessage *m, void *ctx)
{
    HapticsController *hc = (HapticsController *) ctx;
    const char *str = LSMessageGetPayload(m);
    struct json_object *root = str ? json_tokener_parse(str) : NULL;
    struct json_object *json_period;
    struct json_object *json_duration;
    int duration = 0; // infinity
    int period = 0;
    int id;
    char reply[1024] = "{\"returnValue\":false,\"errorText\":\"Invalid arguments\"}";
    LSError lsError;
    LSErrorInit(&lsError);

    if (!root)
        goto error;

    json_period = json_object_object_get(root, "period");
    if (!json_period)
        goto error;
    period = json_object_get_int(json_period);

    json_duration = json_object_object_get(root, "duration");
    if (json_duration)
        duration = json_object_get_int(json_duration);

    id = hc->vibrate(period, duration);
    if (id < 0) {
        snprintf(reply, sizeof(reply), "{\"returnValue\":false,\"errorText\":\"Unable to vibrate\"}");
        goto error;
    }
    if (duration == 0) {
        if (!LSSubscriptionAdd(lh, "com.palm.vibrate/vibrate", m, &lsError)) {
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
        } else {
            hc->addMapping(LSMessageGetUniqueToken(m), id);
        }
    }

    snprintf(reply, sizeof(reply), "{\"returnValue\":true}");
error:
    if (root)
        json_object_put(root);
    if (!LSMessageReply(lh, m, reply, &lsError)) {
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }
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
\param continous If true, the effect plays until the call is cancelled with
       LSCallCancel.
*/
static bool cbVibrateNamedEffect(LSHandle *lh, LSMessage *m, void *ctx)
{
    HapticsController *hc = (HapticsController *) ctx;
    const char *str = LSMessageGetPayload(m);
    struct json_object *root = str ? json_tokener_parse(str) : NULL;
    struct json_object *json_name;
    struct json_object *json_continous;
    const char *name;
    int id;
    char reply[1024] = "{\"returnValue\":false,\"errorText\":\"Invalid arguments\"}";
    LSError lsError;
    LSErrorInit(&lsError);

    if (!root)
        goto error;

    json_name = json_object_object_get(root, "name");
    if (!json_name)
        goto error;
    name = json_object_get_string(json_name);

    id = hc->vibrate(name);
    if (id < 0) {
        snprintf(reply, sizeof(reply), "{\"returnValue\":false,\"errorText\":\"Unable to vibrate\"}");
        goto error;
    }

    json_continous = json_object_object_get(root, "continous");
    if (json_continous && json_object_get_boolean(json_continous)) {
        if (!LSSubscriptionAdd(lh, "com.palm.vibrate/vibrate", m, &lsError)) {
            LSErrorPrint(&lsError, stderr);
            LSErrorFree(&lsError);
        } else {
            hc->addMapping(LSMessageGetUniqueToken(m), id);
        }
    }

    snprintf(reply, sizeof(reply), "{\"returnValue\":true}");
error:
    if (root)
        json_object_put(root);
    if (!LSMessageReply(lh, m, reply, &lsError)) {
        LSErrorPrint(&lsError, stderr);
        LSErrorFree(&lsError);
    }
    return true;
}

static bool cbCancelSubscription(LSHandle *lh, LSMessage *message, void *ctx)
{
    HapticsController *hc = (HapticsController *)ctx;
    int id = hc->getAndRemoveMapping(LSMessageGetUniqueToken(message));
    if (id >= 0)
        hc->cancel(id);
    return true;
}

static LSMethod s_methods[]  = {
    { "vibrate",            cbVibrate },
    { "vibrateNamedEffect", cbVibrateNamedEffect },
    { 0, 0 },
};

bool HapticsController::startService(GMainLoop* mainLoop)
{
    bool result;
    LSError lsError;
    LSErrorInit(&lsError);

    result = LSRegister("com.palm.vibrate", &m_service, &lsError);
    if (!result)
        goto Done;

    result = LSRegisterCategory(m_service, "/", s_methods, NULL, NULL, &lsError);
    if (!result)
        goto Done;

    result = LSCategorySetData(m_service, "/", this, &lsError);
    if (!result)
        goto Done;

    result = LSSubscriptionSetCancelFunction(m_service, cbCancelSubscription, this, &lsError);
    if (!result)
        goto Done;

    result = LSGmainAttach(m_service, mainLoop, &lsError);
    if (!result)
        goto Done;

Done:
    if (!result) {
        g_warning("Failed to start com.palm.vibrate service: %s", lsError.message);
        LSErrorFree(&lsError);
    } else {
        g_debug("com.palm.vibrate service started");
    }
    return result;
}
