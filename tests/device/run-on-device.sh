#!/bin/sh
# A && pass || bad is safe here: pass and bad always succeed.
# shellcheck disable=SC2015
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# run-on-device.sh [iterations]
#
# Run on the device, as root, against the installed LunaHaptics. The motor
# runs: a few short buzzes, and one second-long one per check of a
# cancel. It checks that the service answers and refuses what it should
# (negative values, wrong types), that an infinite vibration stops when its
# caller goes away and is not left running behind a refusal, and that the
# daemon neither restarts nor keeps allocating over two batches of calls
# (iterations each, default 200).
#
# Whether the motor runs is read from the vibrator's sysfs node: the
# timed_output "enable" file (milliseconds left) or the leds class
# "activate" file. Without either, those checks are skipped, not passed.
#
# Needs luna-send (in the image) and busybox; uses gdb when it is there.
set -u

N=${1:-200}
V=luna://com.palm.vibrate
UNIT=luna-haptics
fail=0
pass() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }
skip() { echo "SKIP $*"; }

send() { luna-send -n 1 "$V/$1" "$2" 2>&1; }
pid() { systemctl show -p MainPID --value "$UNIT"; }
restarts() { systemctl show -p NRestarts --value "$UNIT"; }

systemctl is-active -q "$UNIT" || { echo "$UNIT is not running"; exit 2; }
start_pid=$(pid)
start_restarts=$(restarts)

# ---- the motor -----------------------------------------------------------------
MOTOR=""
for f in /sys/class/timed_output/vibrator/enable /sys/class/leds/vibrator/activate; do
	[ -r "$f" ] && { MOTOR=$f; break; }
done
# Running: timed_output reports the milliseconds left, leds "activate" 1.
running() { [ -n "$MOTOR" ] && [ "$(cat "$MOTOR")" -gt 0 ] 2>/dev/null; }
settle() { sleep 1; }

# ---- answers ---------------------------------------------------------------------
r=$(send vibrate '{"period":100,"duration":10}')
case "$r" in *'"returnValue"'*true*) pass "vibrate: the dialpad's blip" ;; *) bad "vibrate dialpad: $r" ;; esac
r=$(send vibrateNamedEffect '{"name":"tapdown"}')
case "$r" in *'"returnValue"'*true*) pass "vibrateNamedEffect tapdown" ;; *) bad "tapdown: $r" ;; esac

refused() {
	r=$(send "$1" "$2")
	case "$r" in
		*'"returnValue"'*false*'"errorText"'*) pass "$1 refuses $2" ;;
		*) bad "$1 $2: $r" ;;
	esac
}
for p in '{"period":200,"duration":-1000}' '{"period":-1}' '{"period":"500"}' '{"period":NaN}' \
         '{}' '[]' 'null' '{"duration":500}'; do
	refused vibrate "$p"
done
for p in '{"name":5}' '{"name":"alert","continous":"false"}' '{}' '{"name":"nosuch"}'; do
	refused vibrateNamedEffect "$p"
done

# Nothing may be running after the refusals.
settle
if [ -z "$MOTOR" ]; then skip "no vibrator node to read"
elif running; then bad "the motor runs after the refusals"
else pass "the motor is off after the refusals"
fi

# ---- an infinite vibration ends with its caller --------------------------------
# luna-send -n 1 takes the one reply and exits; luna-service2 then ends the
# subscription, and the daemon cancels the effect.
r=$(send vibrate '{"period":1000}')
case "$r" in *'"returnValue"'*true*) pass "infinite vibrate accepted" ;; *) bad "infinite vibrate: $r" ;; esac
settle
if [ -z "$MOTOR" ]; then skip "no vibrator node to read"
elif running; then bad "an infinite vibration outlived its caller"
else pass "an infinite vibration stopped when its caller went"
fi

# ---- no growth, no restart -----------------------------------------------------
# A warm-up batch, then two equal batches; the second is judged. A leak of
# one 8-byte allocation per call adds at least 3 * N * 8 bytes to it.
measure() {
	if command -v gdb >/dev/null 2>&1; then
		since=$(date +%s); sleep 1
		gdb -p "$(pid)" -batch -ex 'call (void) malloc_stats()' >/dev/null 2>&1
		sleep 1
		journalctl -u "$UNIT" --since "@$since" --no-pager | grep 'in use bytes' | tail -n 1 | awk '{ print $NF }'
	else
		echo $(( $(awk '/VmRSS/ { print $2 }' "/proc/$(pid)/status") * 1024 ))
	fi
}
rounds() {
	i=0
	while [ $i -lt "$1" ]; do
		send vibrate '{"period":1000}' >/dev/null            # subscribed, then cancelled
		send vibrate '{"period":-1}' >/dev/null              # refused
		send vibrateNamedEffect '{"name":"nosuch"}' >/dev/null  # nyx-side failure path
		i=$((i + 1))
	done
}
command -v gdb >/dev/null 2>&1 && what="malloc in-use bytes" || what="RSS bytes"
rounds $((N / 5 + 1))
m0=$(measure)
rounds "$N"
m1=$(measure)
rounds "$N"
m2=$(measure)
limit=$((3 * N * 8))
case "$m0$m1$m2" in
	''|*[!0-9]*) bad "could not measure memory ($m0 / $m1 / $m2)" ;;
	*)
		if [ $((m2 - m1)) -lt "$limit" ]; then
			pass "$what after warm-up $m0, +$((3 * N)) calls $m1, +$((6 * N)) calls $m2 (a per-call leak adds >= $limit per batch)"
		else
			bad "$what after warm-up $m0, +$((3 * N)) calls $m1, +$((6 * N)) calls $m2: grows by >= $limit per batch"
		fi ;;
esac

settle
if [ -n "$MOTOR" ] && running; then bad "the motor runs after the batches"; fi

[ "$(pid)" = "$start_pid" ] && [ "$(restarts)" = "$start_restarts" ] \
	&& pass "the daemon did not restart" || bad "the daemon restarted"

exit $fail
