#!/bin/sh

script="$1"
test_number="$2"
info_dir="$3"

# This aborts the bisection immediately
die () {
	echo >&2 "error: $*"
	exit 255
}

bisect_head=$(git rev-parse --verify BISECT_HEAD) || die "Failed to find BISECT_HEAD ref"

script_number=$(echo "$script" | sed -e "s/^p\([0-9]*\).*\$/\1/") || die "Failed to get script number for '$script'"

oldtime=$(cat "$info_dir/oldtime") || die "Failed to access '$info_dir/oldtime'"
newtime=$(cat "$info_dir/newtime") || die "Failed to access '$info_dir/newtime'"

cd t/perf || die "Failed to cd into 't/perf'"

result_file="$info_dir/perf_${script_number}_${bisect_head}_results.txt"

GIT_PERF_DIRS_OR_REVS="$bisect_head"
export GIT_PERF_DIRS_OR_REVS

# Don't use codespeed
GIT_PERF_CODESPEED_OUTPUT=
GIT_PERF_SEND_TO_CODESPEED=
export GIT_PERF_CODESPEED_OUTPUT
export GIT_PERF_SEND_TO_CODESPEED

./run "$script" >"$result_file" 2>&1 || die "Failed to run perf test '$script'"

rtime=$(sed -n "s/^$script_number\.$test_number:.*\([0-9]\+\.[0-9]\+\)(.*).*\$/\1/p" "$result_file")

echo "newtime: $newtime"
echo "rtime: $rtime"
echo "oldtime: $oldtime"

# Compare ($newtime - $rtime) with ($rtime - $oldtime)
# Times are decimal number, not integers

if test $(echo "$newtime" "$rtime" "$oldtime" | awk '{ print ($1 - $2 > $2 - $3) }') = 1
then
	# Current commit is considered "good/old"
	echo "$rtime" >"$info_dir/oldtime"
	exit 0
else
	# Current commit is considered "bad/new"
	echo "$rtime" >"$info_dir/newtime"
	exit 1
fi
