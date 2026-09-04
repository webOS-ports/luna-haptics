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

#include "NyxHapticsController.h"

#include <glib.h>
#include <string.h>
#include <limits.h>

NyxHapticsController::NyxHapticsController()
    : m_device(NULL)
{
    nyx_error_t error = nyx_device_open(NYX_DEVICE_HAPTICS, "Main", &m_device);

    if (error != NYX_ERROR_NONE || !m_device) {
        // Keep running: every entry point checks m_device, so a device
        // without a vibrator degrades to "Unable to vibrate" replies
        // instead of a crash.
        m_device = NULL;
        g_critical("NyxHapticsController failed to open the haptics device");
    }
}

NyxHapticsController::~NyxHapticsController()
{
    if (!m_device)
        return;

    if (nyx_device_close(m_device) != NYX_ERROR_NONE)
        g_critical("NyxHapticsController failed to close the haptics device");
}

int NyxHapticsController::cancel(int id)
{
    if (!m_device)
        return -1;

    if (nyx_haptics_cancel(m_device, id) != NYX_ERROR_NONE) {
        g_critical("Failed on nyx_haptics_cancel!");
        return -1;
    }

    return 1;
}

int NyxHapticsController::vibrate(const char *name)
{
    if (!m_device)
        return -1;

    nyx_haptics_configuration_t configuration;
    memset(&configuration, 0, sizeof(configuration));

    if (strcmp(name, "ringtone") == 0) {
        configuration.type = NYX_HAPTICS_EFFECT_RINGTONE;
    } else if (strcmp(name, "alert") == 0) { // long
        configuration.type = NYX_HAPTICS_EFFECT_ALERT;
    } else if (strcmp(name, "notification") == 0) { // short
        configuration.type = NYX_HAPTICS_EFFECT_NOTIFICATION;
    } else if (strcmp(name, "tapdown") == 0) {
        configuration.type = NYX_HAPTICS_EFFECT_TAPDOWN;
    } else if (strcmp(name, "tapup") == 0) {
        configuration.type = NYX_HAPTICS_EFFECT_TAPUP;
    } else {
        return -1;
    }

    if (nyx_haptics_vibrate(m_device, configuration) != NYX_ERROR_NONE) {
        g_critical("Failed on nyx_haptics_vibrate!");
        return -1;
    }

    int id;
    if (nyx_haptics_get_effect_id(m_device, &id) != NYX_ERROR_NONE) {
        g_critical("Failed to obtain haptics effect id!");
        return -1;
    }

    return id;
}

int NyxHapticsController::vibrate(int period, int duration)
{
    if (!m_device)
        return -1;

    if (duration == 0)
        duration = INT_MAX;

    nyx_haptics_configuration_t configuration;
    memset(&configuration, 0, sizeof(configuration));
    configuration.type = NYX_HAPTICS_EFFECT_UNDEFINED;
    configuration.period = period;
    configuration.duration = duration;

    if (nyx_haptics_vibrate(m_device, configuration) != NYX_ERROR_NONE) {
        g_critical("Failed on nyx_haptics_vibrate!");
        return -1;
    }

    int id;
    if (nyx_haptics_get_effect_id(m_device, &id) != NYX_ERROR_NONE) {
        g_critical("Failed to obtain haptics effect id!");
        return -1;
    }

    return id;
}

void NyxHapticsController::cancelAll()
{
    if (!m_device)
        return;

    if (nyx_haptics_cancel_all(m_device) != NYX_ERROR_NONE)
        g_critical("Failed on nyx_haptics_cancel_all!");
}
