luna-haptics
============

Summary
-------
The LuneOS vibration service, serving com.palm.vibrate over nyx

The LuneOS vibration service, serving `com.palm.vibrate` over nyx
(`NYX_DEVICE_HAPTICS`).

API
---

* `com.palm.vibrate/vibrate` — `{"period": ms, "duration": ms}`; omit
  `duration` (or pass 0) to vibrate until the call is cancelled with
  `LSCallCancel`.
* `com.palm.vibrate/vibrateNamedEffect` — `{"name": "ringtone" | "alert" |
  "notification" | "tapdown" | "tapup", "continous": bool}`; a continous
  effect is cancelled with `LSCallCancel`.

History
-------

Split out of [luna-sysmgr](https://github.com/webOS-ports/luna-sysmgr), where
it was `HapticsController` plus `HapticsControllerCastle`. The bus name is
unchanged: cardshell, the Settings app and the phone dialer all call it.

Build
-----

Standard webOS CMake component; depends on glib-2.0, luna-service2, json-c
and nyx.

# Copyright and License Information

Copyright (c) 2008-2013 LG Electronics, Inc.
Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this content except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
