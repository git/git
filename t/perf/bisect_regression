#!/bin/sh

# Read a line coming from `./aggregate.perl --sort-by regression ...`
# and automatically bisect to find the commit responsible for the
# performance regression.
#
# Lines from `./aggregate.perl --sort-by regression ...` look like:
#
# +100.0% p7821-grep-engines-fixed.1 0.04(0.10+0.03) 0.08(0.11+0.08) v2.14.3 v2.15.1
# +33.3% p7820-grep-engines.1 0.03(0.08+0.02) 0.04(0.08+0.02) v2.14.3 v2.15.1
#

die () {
	echo >&2 "error: $*"
	exit 1
}

while [ $# -gt 0 ]; do
	arg="$1"
	case "$arg" in
	--help)
		echo "usage: $0 [--config file] [--subsection subsection]"
		exit 0
		;;
	--config)
		shift
		GIT_PERF_CONFIG_FILE=$(cd "$(dirname "$1")"; pwd)/$(basename "$1")
		export GIT_PERF_CONFIG_FILE
		shift ;;
	--subsection)
		shift
		GIT_PERF_SUBSECTION="$1"
		export GIT_PERF_SUBSECTION
		shift ;;
	--*)
		die "unrecognised option: '$arg'" ;;
	*)
		die "unknown argument '$arg'"
		;;
	esac
done

read -r regression subtest oldtime newtime oldrev newrev

test_script=$(echo "$subtest" | sed -e 's/\(.*\)\.[0-9]*$/\1.sh/')
test_number=$(echo "$subtest" | sed -e 's/.*\.\([0-9]*\)$/\1/')

# oldtime and newtime are decimal number, not integers

oldtime=$(echo "$oldtime" | sed -e 's/^\([0-9]\+\.[0-9]\+\).*$/\1/')
newtime=$(echo "$newtime" | sed -e 's/^\([0-9]\+\.[0-9]\+\).*$/\1/')

test $(echo "$newtime" "$oldtime" | awk '{ print ($1 > $2) }') = 1 ||
	die "New time '$newtime' should be greater than old time '$oldtime'"

tmpdir=$(mktemp -d -t bisect_regression_XXXXXX) || die "Failed to create temp directory"
echo "$oldtime" >"$tmpdir/oldtime" || die "Failed to write to '$tmpdir/oldtime'"
echo "$newtime" >"$tmpdir/newtime" || die "Failed to write to '$tmpdir/newtime'"

# Bisecting must be performed from the top level directory (even with --no-checkout)
(
	toplevel_dir=$(git rev-parse --show-toplevel) || die "Failed to find top level directory"
	cd "$toplevel_dir" || die "Failed to cd into top level directory '$toplevel_dir'"

	git bisect start --no-checkout "$newrev" "$oldrev" || die "Failed to start bisecting"

	git bisect run t/perf/bisect_run_script "$test_script" "$test_number" "$tmpdir"
	res="$?"

	git bisect reset

	exit "$res"
)
