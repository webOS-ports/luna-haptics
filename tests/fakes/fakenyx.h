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

#ifndef FAKENYX_H
#define FAKENYX_H

#include <string>
#include <vector>

#include <nyx/nyx_client.h>

/**
 * nyx's haptics device, in process; the test executable is not linked with
 * nyx-lib. It behaves as the modules in nyx-modules and nyx-modules-hybris
 * do: one effect at a time, a new one replacing whatever plays, effect ids
 * counting up from 1, and a cancel stopping whatever plays whatever id it
 * is given.
 */
namespace fakenyx {

//! Forgets everything recorded and every failure armed; nothing playing.
void reset();

//! Make the next call to the named function fail ("nyx_device_open",
//! "nyx_device_close", "nyx_haptics_vibrate", "nyx_haptics_get_effect_id",
//! "nyx_haptics_cancel", "nyx_haptics_cancel_all").
void failNext(const std::string& function);

//! Every configuration nyx_haptics_vibrate() was given, in order.
const std::vector<nyx_haptics_configuration_t>& vibrations();

//! The ids nyx_haptics_cancel() was given, in order.
const std::vector<int32_t>& cancels();

//! The id of the effect playing, 0 for none.
int32_t playing();

//! Calls made to each nyx function since reset().
int calls(const std::string& function);

//! Devices opened and not closed.
int devicesOpen();

} // namespace fakenyx

#endif // FAKENYX_H
