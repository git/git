#!/bin/sh
#
# An example hook script to verify if you are on battery, in case you
# are running Linux or OS X. Called by git-gc --auto with no arguments.
# The hook should exit with non-zero status after issuing an appropriate
# message if it wants to stop the auto repacking.
#
# This hook is stored in the contrib/hooks directory. Your distribution
# may have put this somewhere else. If you want to use this hook, you
# should make this script executable then link to it in the repository
# you would like to use it in.
#
# For example, if the hook is stored in
# /usr/share/git-core/contrib/hooks/pre-auto-gc-battery:
#
# cd /path/to/your/repository.git
# ln -sf /usr/share/git-core/contrib/hooks/pre-auto-gc-battery \
#	hooks/pre-auto-gc

if test -x /sbin/on_ac_power && (/sbin/on_ac_power;test $? -ne 1)
then
	exit 0
elif test "$(cat /sys/class/power_supply/AC/online 2>/dev/null)" = 1
then
	exit 0
elif grep -q 'on-line' /proc/acpi/ac_adapter/AC/state 2>/dev/null
then
	exit 0
elif grep -q '0x01$' /proc/apm 2>/dev/null
then
	exit 0
elif grep -q "AC Power \+: 1" /proc/pmu/info 2>/dev/null
then
	exit 0
elif test -x /usr/bin/pmset && /usr/bin/pmset -g batt |
	grep -q "drawing from 'AC Power'"
then
	exit 0
fi

echo "Auto packing deferred; not on AC"
exit 1
