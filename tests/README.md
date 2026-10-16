luna-haptics test and hardening harness
=======================================

Four layers: unit tests on the build host, the sanitizers and valgrind over
them, static analysis and a warning gate over the sources, and a run against
the daemon on a device.

Every script takes a Yocto recipe workdir (`tmp/work/<arch>/<recipe>/<ver>`)
for the toolchain and the sysroot. Any recipe whose sysroot has glib, json-c,
luna-service2, nyx and googletest will do. With `rm_work`, luna-haptics's own
workdir is gone once it is built; luna-displaymanager's has all five. The
corei7-64 one is the one the host can run.

Unit tests (`unit/`, `fakes/`)
------------------------------

gtest, built from the daemon's own objects (the `haptics-core` object
library, everything but `main()`) and linked with stand-ins instead of the
real libraries:

- `fakes/fakebus.cpp`: luna-service2, in process. It records registrations
  and replies, calls the daemon's methods as a client would (`callMethod`),
  plays the client cancelling a call or going away (`cancel`, `disconnect`),
  refuses a subscription for a client that has already gone as the real
  library does, fails any LS call on request (`failNext`), and counts the
  LSErrors and handles still outstanding. Each message is freed when its
  call returns, or when its subscription ends, so a token kept past that is
  a use after free the sanitizers see.
- `fakes/fakenyx.cpp`: the nyx haptics device, behaving as the modules in
  nyx-modules and nyx-modules-hybris do: one effect at a time, a new one
  replacing the last, and a cancel stopping whatever plays whatever id it is
  given.

Every test's TearDown checks that no handle, LSError or open device is left,
and that nothing is still vibrating once the daemon has gone. Every test also
fails on any critical or warning from GLib itself: a `g_return_if_fail()`
tripping inside glib is the only sign of a misuse there, as glib is not built
with the sanitizers.

| File | What it pins |
|---|---|
| `test_vibrate.cpp` | period and duration passed on; what the callers in the tree send; an omitted or 0 duration being infinite and tied to the call; negative, non-numeric, NaN and non-object payloads refused before nyx; large numbers clamped; an infinite vibration stopped, not left running, when its caller has gone or cannot be subscribed, or nyx gives no effect id |
| `test_named.cpp` | each name to its nyx effect; unknown names failing; `name` a string and `continous` a boolean (json-c reads `"false"` as true); a continous effect tied to the call, and stopped if that fails |
| `test_subscriptions.cpp` | a cancel or the caller going away stopping the vibration; an older subscription ending leaving a newer one's vibration alone; a caller with several subscriptions going; fifty callers ending in any order; destroy() stopping an open one |
| `test_lifecycle.cpp` | registration of both methods and the cancel function; every startService() step failing cleanly; destroy() stopping the motor, closing the device and unregistering, once; no device still serving; the installed `files/` (API, role, groups, perm, D-Bus and systemd services) naming exactly what the code registers |
| `test_daemon.cpp` | `runHaptics()`, all of `main()`: serves until a real SIGTERM or SIGINT, then stops the motor and exits 0; exits 1 when the start fails at any step, with the partial registration dropped; leaves no signal source behind to fire on a freed main loop |

Build and run on the host:

    cmake -DBUILD_TESTING=ON ...      # with the recipe's toolchain.cmake
    ninja haptics-tests
    <sysroot>/usr/lib/ld-linux-x86-64.so.2 --library-path <sysroot>/usr/lib \
        tests/haptics-tests

On a device, copy the armv7 or aarch64 build (gtest is linked statically)
together with `files/`, and point the tests at the copy:

    HAPTICS_SOURCE_DIR=<dir with files/> ./haptics-tests

Hardening (`hardening/`)
------------------------

- `run-sanitizers.sh <corei7-64 workdir>`: the tests under ASan+UBSan and
  TSan, each built with gcc and with clang, and under valgrind memcheck on
  the image toolchain's own build. clang also gets its integer and
  nullability checks; its unsigned wrap-around check is left out because
  libstdc++ relies on unsigned wrap-around, which is defined behaviour. The
  image toolchain ships no sanitizer runtimes, so the host's gcc and clang
  build against the sysroot (`host-sysroot.cmake`); the host's glibc is the
  sysroot's.
- `static-analysis.sh <corei7-64 workdir>`: gcc `-fanalyzer` at -O0 and -O2,
  the clang analyzer with the alpha checkers, clang-tidy, cppcheck
  (exhaustive, inconclusive), flawfinder and Coccinelle (`cocci/`). It fails
  on any finding not in `baseline.txt`, where each entry that was looked at
  and left has its reason. cppcheck's style notes are listed but do not
  gate. sparse is not run: it parses C only.
- `build-all.py <workdir>...`: the warning gate. The daemon and the tests
  are built for each architecture given, with the image's gcc and with the
  host's clang, at `-Wall -Wextra`; a warning in `Src/` or `tests/` fails it.
  Run it with the armv7, aarch64 and corei7-64 workdirs.
- `mutation-check.py <corei7-64 workdir>`: shows the tests can fail. It
  builds seventeen mutants of `Src/`, each putting back one fault the daemon
  had or could have: negative or wrongly typed values passed to nyx, NaN
  cast to int, an infinite vibration left running (or reported as fine) when
  it cannot be tied to its caller, an older subscription's end stopping a
  newer vibration, the motor left running or
  the device left open at exit, a handle or LSError leaked, main()'s old
  signal handling, and a signal that does not stop the loop. Each must be
  caught by a test, by ASan, by a timeout, or, where the fault is inside
  uninstrumented code, by GLib's own critical.

The Coccinelle rules flag a json member read without its type checked (all
four reads in the daemon before this harness) and a message token stored
without a copy. They are checked against `cocci/selftest.cpp`, which has the
patterns they look for, so a rule that silently matches nothing fails the
gate instead of passing it.

On the device (`device/`)
-------------------------

`run-on-device.sh [iterations]`, as root on the device, against the installed
daemon. The motor runs: a few short buzzes. It checks that the service
answers the callers' payloads and refuses hostile ones, that nothing is left
vibrating after a refusal, that an infinite vibration stops when its caller
(`luna-send -n 1`) goes, and that the daemon neither restarts nor keeps
allocating. Whether the motor runs is read from the vibrator's sysfs node
(timed_output `enable`, or leds-class `activate`).

For memory, it runs a warm-up batch, then two equal batches of calls, and
judges the second: a leak of even one 8-byte allocation per call adds at
least `3 * iterations * 8` bytes to it. With gdb on the device the measure
is malloc's own count of bytes in use (`malloc_stats()`, read back from the
journal), not RSS.
