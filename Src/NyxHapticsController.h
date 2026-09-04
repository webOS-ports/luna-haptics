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

#ifndef __NYX_HAPTICS_CONTROLLER_H__
#define __NYX_HAPTICS_CONTROLLER_H__

#include "HapticsController.h"

#include <nyx/nyx_client.h>

/**
 * The nyx NYX_DEVICE_HAPTICS backend. Was HapticsControllerCastle in
 * luna-sysmgr - named for the Pre it was written on, but nothing in it is
 * device-specific; nyx is the portability layer.
 */
class NyxHapticsController : public HapticsController
{
public:
    NyxHapticsController();
    ~NyxHapticsController() override;

    int cancel(int id) override;
    int vibrate(int period, int duration) override;
    int vibrate(const char *name) override;
    void cancelAll() override;

private:
    nyx_device_handle_t m_device;
};

#endif
