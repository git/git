# Performance testing framework.  Each perf script starts much like
# a normal test script, except it sources this library instead of
# test-lib.sh.  See t/perf/README for documentation.
#
# Copyright (c) 2011 Thomas Rast
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see http://www.gnu.org/licenses/ .

# These variables must be set before the inclusion of test-lib.sh below,
# because it will change our working directory.
TEST_DIRECTORY=$(pwd)/..
TEST_OUTPUT_DIRECTORY=$(pwd)

TEST_NO_CREATE_REPO=t
TEST_NO_MALLOC_CHECK=t

. ../test-lib.sh

unset BUT_CONFIG_NOSYSTEM
BUT_CONFIG_SYSTEM="$TEST_DIRECTORY/perf/config"
export BUT_CONFIG_SYSTEM

if test -n "$BUT_TEST_INSTALLED" -a -z "$PERF_SET_BUT_TEST_INSTALLED"
then
	error "Do not use BUT_TEST_INSTALLED with the perf tests.

Instead use:

    ./run <path-to-but> -- <tests>

See t/perf/README for details."
fi

# Variables from test-lib that are normally internal to the tests; we
# need to export them for test_perf subshells
export TEST_DIRECTORY TRASH_DIRECTORY BUT_BUILD_DIR BUT_TEST_CMP

MODERN_BUT=$BUT_BUILD_DIR/bin-wrappers/but
export MODERN_BUT

perf_results_dir=$TEST_RESULTS_DIR
test -n "$BUT_PERF_SUBSECTION" && perf_results_dir="$perf_results_dir/$BUT_PERF_SUBSECTION"
mkdir -p "$perf_results_dir"
rm -f "$perf_results_dir"/$(basename "$0" .sh).subtests

die_if_build_dir_not_repo () {
	if ! ( cd "$TEST_DIRECTORY/.." &&
		    but rev-parse --build-dir >/dev/null 2>&1 ); then
		error "No $1 defined, and your build directory is not a repo"
	fi
}

if test -z "$BUT_PERF_REPO"; then
	die_if_build_dir_not_repo '$BUT_PERF_REPO'
	BUT_PERF_REPO=$TEST_DIRECTORY/..
fi
if test -z "$BUT_PERF_LARGE_REPO"; then
	die_if_build_dir_not_repo '$BUT_PERF_LARGE_REPO'
	BUT_PERF_LARGE_REPO=$TEST_DIRECTORY/..
fi

test_perf_do_repo_symlink_config_ () {
	test_have_prereq SYMLINKS || but config core.symlinks false
}

test_perf_copy_repo_contents () {
	for stuff in "$1"/*
	do
		case "$stuff" in
		*/objects|*/hooks|*/config|*/commondir|*/butdir|*/worktrees|*/fsmonitor--daemon*)
			;;
		*)
			cp -R "$stuff" "$repo/.but/" || exit 1
			;;
		esac
	done
}

test_perf_create_repo_from () {
	test "$#" = 2 ||
	BUG "not 2 parameters to test-create-repo"
	repo="$1"
	source="$2"
	source_but="$("$MODERN_BUT" -C "$source" rev-parse --but-dir)"
	objects_dir="$("$MODERN_BUT" -C "$source" rev-parse --but-path objects)"
	common_dir="$("$MODERN_BUT" -C "$source" rev-parse --but-common-dir)"
	mkdir -p "$repo/.but"
	(
		cd "$source" &&
		{ cp -Rl "$objects_dir" "$repo/.but/" 2>/dev/null ||
			cp -R "$objects_dir" "$repo/.but/"; } &&

		# common_dir must come first here, since we want source_but to
		# take precedence and overwrite any overlapping files
		test_perf_copy_repo_contents "$common_dir"
		if test "$source_but" != "$common_dir"
		then
			test_perf_copy_repo_contents "$source_but"
		fi
	) &&
	(
		cd "$repo" &&
		"$MODERN_BUT" init -q &&
		test_perf_do_repo_symlink_config_ &&
		mv .but/hooks .but/hooks-disabled 2>/dev/null &&
		if test -f .but/index.lock
		then
			# We may be copying a repo that can't run "but
			# status" due to a locked index. Since we have
			# a copy it's fine to remove the lock.
			rm .but/index.lock
		fi
	) || error "failed to copy repository '$source' to '$repo'"
}

# call at least one of these to establish an appropriately-sized repository
test_perf_fresh_repo () {
	repo="${1:-$TRASH_DIRECTORY}"
	"$MODERN_BUT" init -q "$repo" &&
	(
		cd "$repo" &&
		test_perf_do_repo_symlink_config_
	)
}

test_perf_default_repo () {
	test_perf_create_repo_from "${1:-$TRASH_DIRECTORY}" "$BUT_PERF_REPO"
}
test_perf_large_repo () {
	if test "$BUT_PERF_LARGE_REPO" = "$BUT_BUILD_DIR"; then
		echo "warning: \$BUT_PERF_LARGE_REPO is \$BUT_BUILD_DIR." >&2
		echo "warning: This will work, but may not be a sufficiently large repo" >&2
		echo "warning: for representative measurements." >&2
	fi
	test_perf_create_repo_from "${1:-$TRASH_DIRECTORY}" "$BUT_PERF_LARGE_REPO"
}
test_checkout_worktree () {
	but checkout-index -u -a ||
	error "but checkout-index failed"
}

# Performance tests should never fail.  If they do, stop immediately
immediate=t

# Perf tests require GNU time
case "$(uname -s)" in Darwin) GTIME="${GTIME:-gtime}";; esac
GTIME="${GTIME:-/usr/bin/time}"

test_run_perf_ () {
	test_cleanup=:
	test_export_="test_cleanup"
	export test_cleanup test_export_
	"$GTIME" -f "%E %U %S" -o test_time.$i "$TEST_SHELL_PATH" -c '
. '"$TEST_DIRECTORY"/test-lib-functions.sh'
test_export () {
	test_export_="$test_export_ $*"
}
'"$1"'
ret=$?
needles=
for v in $test_export_
do
	needles="$needles;s/^$v=/export $v=/p"
done
set | sed -n "s'"/'/'\\\\''/g"'$needles" >test_vars
exit $ret' >&3 2>&4
	eval_ret=$?

	if test $eval_ret = 0 || test -n "$expecting_failure"
	then
		test_eval_ "$test_cleanup"
		. ./test_vars || error "failed to load updated environment"
	fi
	if test "$verbose" = "t" && test -n "$HARNESS_ACTIVE"; then
		echo ""
	fi
	return "$eval_ret"
}

test_wrapper_ () {
	test_wrapper_func_=$1; shift
	test_start_
	test "$#" = 3 && { test_prereq=$1; shift; } || test_prereq=
	test "$#" = 2 ||
	BUG "not 2 or 3 parameters to test-expect-success"
	export test_prereq
	if ! test_skip "$@"
	then
		base=$(basename "$0" .sh)
		echo "$test_count" >>"$perf_results_dir"/$base.subtests
		echo "$1" >"$perf_results_dir"/$base.$test_count.descr
		base="$perf_results_dir"/"$PERF_RESULTS_PREFIX$(basename "$0" .sh)"."$test_count"
		"$test_wrapper_func_" "$@"
	fi

	test_finish_
}

test_perf_ () {
	if test -z "$verbose"; then
		printf "%s" "perf $test_count - $1:"
	else
		echo "perf $test_count - $1:"
	fi
	for i in $(test_seq 1 $BUT_PERF_REPEAT_COUNT); do
		say >&3 "running: $2"
		if test_run_perf_ "$2"
		then
			if test -z "$verbose"; then
				printf " %s" "$i"
			else
				echo "* timing run $i/$BUT_PERF_REPEAT_COUNT:"
			fi
		else
			test -z "$verbose" && echo
			test_failure_ "$@"
			break
		fi
	done
	if test -z "$verbose"; then
		echo " ok"
	else
		test_ok_ "$1"
	fi
	"$TEST_DIRECTORY"/perf/min_time.perl test_time.* >"$base".result
	rm test_time.*
}

test_perf () {
	test_wrapper_ test_perf_ "$@"
}

test_size_ () {
	say >&3 "running: $2"
	if test_eval_ "$2" 3>"$base".result; then
		test_ok_ "$1"
	else
		test_failure_ "$@"
	fi
}

test_size () {
	test_wrapper_ test_size_ "$@"
}

# We extend test_done to print timings at the end (./run disables this
# and does it after running everything)
test_at_end_hook_ () {
	if test -z "$BUT_PERF_AGGREGATING_LATER"; then
		(
			cd "$TEST_DIRECTORY"/perf &&
			./aggregate.perl --results-dir="$TEST_RESULTS_DIR" $(basename "$0")
		)
	fi
}

test_export () {
	export "$@"
}

test_lazy_prereq PERF_EXTRA 'test_bool_env BUT_PERF_EXTRA false'
