#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# mutation-check.py [--work DIR] <corei7-64 recipe workdir>
#
# Shows the tests can fail: each mutant below breaks one behaviour they are
# meant to pin, in a copy of the tree, and the ASan+UBSan build (host gcc on
# the recipe's sysroot, as run-sanitizers.sh builds it) has to fail on it -
# through an assertion, or through ASan/LSan for the ownership mutants. The
# unmutated copy has to pass first, or nothing below means anything.

import argparse, glob, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.expanduser("~/claude-scratch/luna-haptics/mutants"))
ap.add_argument("workdir")
opts = ap.parse_args()
# Each verdict as it comes, also when the output is a file.
sys.stdout.reconfigure(line_buffering=True)

SRC = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CPP = "Src/HapticsController.cpp"
HDR = "Src/HapticsController.h"
NYX = "Src/NyxHapticsController.cpp"
DAEMON = "Src/Daemon.cpp"

# name, file, text, replacement, what should catch it (file, text and
# replacement may be parallel lists, for a mutant that needs more than one
# edit)
MUTANTS = [
    # What luna-sysmgr and the split-out daemon did: any number goes to nyx.
    ("negative-passed-to-nyx", CPP,
     "        int32_t n = json_object_get_int(value);\n        if (n < 0)\n            return false;\n",
     "        int32_t n = json_object_get_int(value);\n",
     "Vibrate.RefusesNegativeValues"),
    ("any-type-read-as-number", CPP,
     "        return true;\n    }\n\n    return false;\n}\n\nstatic bool getString",
     "        return true;\n    }\n\n    *out = json_object_get_int(value);\n    return true;\n}\n\nstatic bool getString",
     "Vibrate.RefusesWhatIsNotANumber"),
    ("nan-cast-to-int", CPP, "        if (!(d >= 0))", "        if (d < 0)",
     "Vibrate.NonFiniteNumbers"),
    ("name-type-unchecked", CPP,
     "    if (!json_object_is_type(value, json_type_string))\n        return false;\n", "",
     "NamedEffect.NameMustBeAString"),
    ("continous-type-unchecked", CPP,
     "    if (!json_object_is_type(value, json_type_boolean))\n        return false;\n", "",
     "NamedEffect.ContinousMustBeABoolean"),
    # The two halves of what the old handlers did when LSSubscriptionAdd failed.
    ("unsubscribed-infinite-keeps-playing", CPP, "        hc->cancel(id);\n        return false;", "        return false;",
     "InfiniteVibrationForAGoneCallerIsStopped"),
    ("unsubscribed-infinite-reports-ok", CPP,
     "(duration != 0 || subscribe(hc, lh, m, id))",
     "(duration != 0 || subscribe(hc, lh, m, id) || true)",
     "Vibrate.InfiniteVibrationForAGoneCallerIsStopped"),
    ("stale-cancel-stops-newer", CPP, "        if (newest)\n            cancel(id);", "        cancel(id);",
     "Subscriptions.OlderSubscriptionEndingLeavesTheNewerVibration"),
    # Not here: keeping the token as the message's own pointer instead of a
    # copy. It is equivalent under luna-service2's rules - a subscribed
    # message lives until its subscription ends, the cancel function runs
    # before it is released, and the daemon only reads the tokens of open
    # subscriptions - so no test can fail on it. The copy stays, as the
    # cheap way not to depend on that; cocci/token-kept-uncopied.cocci
    # guards the pattern.
    ("effect-without-id-keeps-playing", NYX, "        nyx_haptics_cancel(m_device, 0);\n", "",
     "Vibrate.NoEffectIdStopsTheEffect"),
    ("close-leaves-motor-running", NYX, "    cancelAll();\n\n    if (nyx_device_close", "    if (nyx_device_close",
     "still vibrating after the daemon went"),
    ("device-not-closed", NYX,
     "    if (nyx_device_close(m_device) != NYX_ERROR_NONE)\n        g_critical(\"NyxHapticsController failed to close the haptics device\");\n",
     "", "the nyx device was not closed"),
    ("destructor-keeps-handle", CPP, "    if (m_service) {\n        LSError lsError;",
     "    if (false && m_service) {\n        LSError lsError;",
     "an LSHandle was not unregistered"),
    ("unregister-error-not-freed", CPP,
     "            g_warning(\"Failed to unregister com.palm.vibrate: %s\", lsError.message);\n            LSErrorFree(&lsError);",
     "            g_warning(\"Failed to unregister com.palm.vibrate: %s\", lsError.message);",
     "Lifecycle.FailedUnregisterFreesItsError"),
    ("start-error-not-freed", CPP,
     "        g_warning(\"Failed to start com.palm.vibrate service: %s\", lsError.message);\n        LSErrorFree(&lsError);",
     "        g_warning(\"Failed to start com.palm.vibrate service: %s\", lsError.message);",
     "StartFailure.ReportedAndCleanedUp"),
    # What main() did before: each signal source removed only when it fires,
    # so the other one outlives its main loop.
    ("signal-source-outlives-loop", DAEMON,
     ["    return G_SOURCE_CONTINUE;\n", "    g_source_remove(sigterm);\n    g_source_remove(sigint);\n"],
     ["    return G_SOURCE_REMOVE;\n", ""],
     # The stale source reads its freed loop inside glib, which ASan does
     # not see; glib's own ref_count check does, and the fixture fails on it.
     "GLib complained: g_main_loop_quit"),
    ("failed-start-keeps-handle", DAEMON,
     "    HapticsController::destroy();\n\n    g_source_remove(sigterm);",
     "    if (status == 0)\n        HapticsController::destroy();\n\n    g_source_remove(sigterm);",
     "DaemonStartFailure.ExitsOneAndCleansUp"),
    ("signal-does-not-quit", DAEMON,
     "    g_main_loop_quit(static_cast<GMainLoop*>(user_data));\n", "",
     "timeout"),
]

S = os.path.join(opts.workdir, "recipe-sysroot")
N = os.path.join(opts.workdir, "recipe-sysroot-native")
env = dict(os.environ, PATH=os.path.join(N, "usr/bin") + os.pathsep + os.environ["PATH"],
           PKG_CONFIG_SYSROOT_DIR=S,
           PKG_CONFIG_LIBDIR=S + "/usr/lib/pkgconfig:" + S + "/usr/share/pkgconfig",
           PKG_CONFIG_PATH=S + "/usr/lib/pkgconfig",
           ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:fast_unwind_on_malloc=0",
           UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1",
           G_SLICE="always-malloc", DEBUGINFOD_URLS="")

rt = os.path.join(opts.work, "hostrt")
os.makedirs(rt, exist_ok=True)
for lib in ("libasan.so.8", "libubsan.so.1", "libstdc++.so.6", "libgcc_s.so.1"):
    f = subprocess.run(["gcc", "-print-file-name=" + lib], capture_output=True, text=True).stdout.strip()
    if os.path.exists(f):
        dst = os.path.join(rt, lib)
        if os.path.lexists(dst):
            os.remove(dst)
        os.symlink(os.path.realpath(f), dst)


def build_and_run(name, mutate=None):
    tree = os.path.join(opts.work, name, "src")
    build = os.path.join(opts.work, name, "build")
    shutil.rmtree(tree, ignore_errors=True)
    shutil.copytree(SRC, tree, ignore=shutil.ignore_patterns(".git"))
    if mutate:
        paths, olds, news = mutate
        # A mutant is one edit, or several given as parallel lists; the file
        # may be one for all of them, or a list of its own.
        if isinstance(olds, str):
            olds, news = [olds], [news]
        if isinstance(paths, str):
            paths = [paths] * len(olds)
        for path, old, new in zip(paths, olds, news):
            p = os.path.join(tree, path)
            text = open(p).read()
            if text.count(old) != 1:
                return None, "mutation does not apply in %s (%d matches)" % (path, text.count(old))
            open(p, "w").write(text.replace(old, new))
    cfg = subprocess.run(["cmake", "-S", tree, "-B", build, "-G", "Ninja",
                          "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(tree, "tests/hardening/host-sysroot.cmake"),
                          "-DHAPTICS_SYSROOT=" + S, "-DHAPTICS_HOST_CC=gcc", "-DWEBOS_INSTALL_ROOT=/",
                          "-DBUILD_TESTING=ON",
                          "-DHAPTICS_SANITIZE=address,undefined", "-DCMAKE_CXX_FLAGS=-g -O1",
                          "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--dynamic-linker=%s/usr/lib/ld-linux-x86-64.so.2 "
                          "-Wl,--disable-new-dtags -Wl,-rpath,%s:%s/usr/lib" % (S, rt, S)],
                         capture_output=True, text=True, env=env)
    if cfg.returncode:
        return None, "configure failed:\n" + cfg.stdout[-2000:] + cfg.stderr[-2000:]
    b = subprocess.run(["ninja", "-C", build, "haptics-tests"], capture_output=True, text=True, env=env)
    if b.returncode:
        return None, "build failed:\n" + b.stdout[-2000:]
    try:
        r = subprocess.run([os.path.join(build, "tests/haptics-tests")], capture_output=True, text=True,
                           env=env, timeout=120)
    except subprocess.TimeoutExpired:
        return -1, "timeout: the tests did not finish"
    return r.returncode, r.stdout + r.stderr


fail = False
rc, out = build_and_run("unmutated")
if rc != 0:
    print("FAIL the unmutated tree does not pass:\n" + (out or "")[-3000:])
    sys.exit(1)
print("ok   unmutated tree passes")

for name, path, old, new, catcher in MUTANTS:
    rc, out = build_and_run(name, (path, old, new))
    if rc is None:
        print("FAIL %-26s %s" % (name, out))
        fail = True
    elif rc == 0:
        print("FAIL %-26s survived: no test failed" % name)
        fail = True
    elif catcher not in out:
        print("ok   %-26s killed, but not by %s" % (name, catcher))
    else:
        print("ok   %-26s killed by %s" % (name, catcher))

sys.exit(1 if fail else 0)
