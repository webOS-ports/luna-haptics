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

#include "fakenyx.h"

#include <map>
#include <set>

// Opaque to the daemon; any definition will do here.
struct nyx_device {
    int open;
};

namespace {

std::set<std::string> s_failNext;
std::vector<nyx_haptics_configuration_t> s_vibrations;
std::vector<int32_t> s_cancels;
std::map<std::string, int> s_calls;
int32_t s_playing = 0;
int32_t s_counter = 0;
int s_devices = 0;

bool fail(const char* function)
{
    ++s_calls[function];
    return s_failNext.erase(function) > 0;
}

} // namespace

namespace fakenyx {

void reset()
{
    s_failNext.clear();
    s_vibrations.clear();
    s_cancels.clear();
    s_calls.clear();
    s_playing = 0;
    s_counter = 0;
    // A device the daemon did not close stays counted.
}

void failNext(const std::string& function) { s_failNext.insert(function); }
const std::vector<nyx_haptics_configuration_t>& vibrations() { return s_vibrations; }
const std::vector<int32_t>& cancels() { return s_cancels; }
int32_t playing() { return s_playing; }
int calls(const std::string& function) { return s_calls[function]; }
int devicesOpen() { return s_devices; }

} // namespace fakenyx

nyx_error_t nyx_device_open(nyx_device_type_t type, nyx_device_id_t id, nyx_device_handle_t* handle_out_ptr)
{
    if (fail(__func__))
        return NYX_ERROR_DEVICE_NOT_EXIST;
    if (type != NYX_DEVICE_HAPTICS || std::string(id) != "Main")
        return NYX_ERROR_DEVICE_NOT_EXIST;

    *handle_out_ptr = new nyx_device{1};
    ++s_devices;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_device_close(nyx_device_handle_t handle)
{
    // nyx-lib frees the device whatever the module reports.
    delete handle;
    --s_devices;
    return fail(__func__) ? NYX_ERROR_GENERIC : NYX_ERROR_NONE;
}

nyx_error_t nyx_haptics_vibrate(nyx_device_handle_t handle, nyx_haptics_configuration_t configuration)
{
    if (!handle || !handle->open)
        return NYX_ERROR_INVALID_HANDLE;
    if (fail(__func__))
        return NYX_ERROR_INVALID_VALUE;

    s_vibrations.push_back(configuration);
    s_playing = ++s_counter;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_haptics_get_effect_id(nyx_device_handle_t handle, int32_t* haptics_effect_id_out_ptr)
{
    if (!handle || !handle->open)
        return NYX_ERROR_INVALID_HANDLE;
    if (fail(__func__))
        return NYX_ERROR_GENERIC;

    *haptics_effect_id_out_ptr = s_counter;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_haptics_cancel(nyx_device_handle_t handle, int32_t haptics_id)
{
    if (!handle || !handle->open)
        return NYX_ERROR_INVALID_HANDLE;
    if (fail(__func__))
        return NYX_ERROR_GENERIC;

    s_cancels.push_back(haptics_id);
    s_playing = 0;
    return NYX_ERROR_NONE;
}

nyx_error_t nyx_haptics_cancel_all(nyx_device_handle_t handle)
{
    if (!handle || !handle->open)
        return NYX_ERROR_INVALID_HANDLE;
    if (fail(__func__))
        return NYX_ERROR_GENERIC;

    s_playing = 0;
    return NYX_ERROR_NONE;
}
