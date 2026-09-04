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

#ifndef __HAPTICS_CONTROLLER_H__
#define __HAPTICS_CONTROLLER_H__

#include <glib.h>
#include <luna-service2/lunaservice.h>

/**
 * Serves com.palm.vibrate: vibrate (period/duration) and vibrateNamedEffect.
 *
 * An infinite vibration (duration 0, or continous:true) is tied to the
 * caller's subscription: LSCallCancel from the client cancels the effect,
 * through the token->effect-id mapping kept here.
 *
 * Split out of luna-sysmgr when that component was dissolved into
 * per-function daemons. The backend below this interface is nyx
 * (NYX_DEVICE_HAPTICS); see NyxHapticsController.
 */
class HapticsController
{
public:
    static HapticsController *instance();

    virtual ~HapticsController() { }

    virtual int vibrate(int period, int duration) { return -1; }
    virtual int vibrate(const char *name) { return -1; }
    virtual int cancel(int id) { return -1; }
    virtual void cancelAll() {}

    void addMapping(const char *key, int id) {
        g_hash_table_insert(m_mappingTable, g_strdup(key), GINT_TO_POINTER(id));
    }
    int getAndRemoveMapping(const char *key) {
        gpointer value = NULL;
        if (!g_hash_table_lookup_extended(m_mappingTable, key, NULL, &value))
            return -1;
        g_hash_table_remove(m_mappingTable, key);
        return GPOINTER_TO_INT(value);
    }

    bool startService(GMainLoop* mainLoop);

protected:
    HapticsController();
    LSHandle *m_service;

private:
    static HapticsController *s_instance;
    GHashTable *m_mappingTable;
};

#endif
