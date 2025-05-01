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
# along with this program.  If not, see https://www.gnu.org/licenses/ .

# These variables must be set before the inclusion of test-lib.sh below,
# because it will change our working directory.
TEST_DIRECTORY=$(pwd)/..
TEST_OUTPUT_DIRECTORY=$(pwd)

TEST_NO_CREATE_REPO=t
TEST_NO_MALLOC_CHECK=t

# GIT-BUILD-OPTIONS, sourced by test-lib.sh, overwrites the `GIT_PERF_*`
# values that are set by the user (if any). Let's stash them away as
# `eval`-able assignments.
git_perf_settings="$(env |
	sed -n "/^GIT_PERF_/{
		# escape all single-quotes in the value
		s/'/'\\\\''/g
		# turn this into an eval-able assignment
		s/^\\([^=]*=\\)\\(.*\\)/\\1'\\2'/p
	}")"

. ../test-lib.sh
eval "$git_perf_settings"

unset GIT_CONFIG_NOSYSTEM
GIT_CONFIG_SYSTEM="$TEST_DIRECTORY/perf/config"
export GIT_CONFIG_SYSTEM

if test -n "$GIT_TEST_INSTALLED" && test -z "$PERF_SET_GIT_TEST_INSTALLED"
then
	error "Do not use GIT_TEST_INSTALLED with the perf tests.

Instead use:

    ./run <path-to-git> -- <tests>

See t/perf/README for details."
fi

# Variables from test-lib that are normally internal to the tests; we
# need to export them for test_perf subshells
export TEST_DIRECTORY TRASH_DIRECTORY GIT_BUILD_DIR GIT_TEST_CMP

MODERN_GIT=$GIT_BUILD_DIR/bin-wrappers/git
export MODERN_GIT

MODERN_SCALAR=$GIT_BUILD_DIR/bin-wrappers/scalar
export MODERN_SCALAR

perf_results_dir=$TEST_RESULTS_DIR
test -n "$GIT_PERF_SUBSECTION" && perf_results_dir="$perf_results_dir/$GIT_PERF_SUBSECTION"
mkdir -p "$perf_results_dir"
rm -f "$perf_results_dir"/$(basename "$0" .sh).subtests

die_if_build_dir_not_repo () {
	if ! ( cd "$TEST_DIRECTORY/.." &&
		    git rev-parse --build-dir >/dev/null 2>&1 ); then
		error "No $1 defined, and your build directory is not a repo"
	fi
}

if test -z "$GIT_PERF_REPO"; then
	die_if_build_dir_not_repo '$GIT_PERF_REPO'
	GIT_PERF_REPO=$TEST_DIRECTORY/..
fi
if test -z "$GIT_PERF_LARGE_REPO"; then
	die_if_build_dir_not_repo '$GIT_PERF_LARGE_REPO'
	GIT_PERF_LARGE_REPO=$TEST_DIRECTORY/..
fi

test_perf_do_repo_symlink_config_ () {
	test_have_prereq SYMLINKS || git config core.symlinks false
}

test_perf_copy_repo_contents () {
	for stuff in "$1"/*
	do
		case "$stuff" in
		*/objects|*/hooks|*/config|*/commondir|*/gitdir|*/worktrees|*/fsmonitor--daemon*)
			;;
		*)
			cp -R "$stuff" "$repo/.git/" || exit 1
			;;
		esac
	done
}

test_perf_create_repo_from () {
	test "$#" = 2 ||
	BUG "not 2 parameters to test-create-repo"
	repo="$1"
	source="$2"
	source_git="$("$MODERN_GIT" -C "$source" rev-parse --git-dir)"
	objects_dir="$("$MODERN_GIT" -C "$source" rev-parse --git-path objects)"
	common_dir="$("$MODERN_GIT" -C "$source" rev-parse --git-common-dir)"
	mkdir -p "$repo/.git"
	(
		cd "$source" &&
		{ cp -Rl "$objects_dir" "$repo/.git/" 2>/dev/null ||
			cp -R "$objects_dir" "$repo/.git/"; } &&

		# common_dir must come first here, since we want source_git to
		# take precedence and overwrite any overlapping files
		test_perf_copy_repo_contents "$common_dir"
		if test "$source_git" != "$common_dir"
		then
			test_perf_copy_repo_contents "$source_git"
		fi
	) &&
	(
		cd "$repo" &&
		"$MODERN_GIT" init -q &&
		test_perf_do_repo_symlink_config_ &&
		mv .git/hooks .git/hooks-disabled 2>/dev/null &&
		if test -f .git/index.lock
		then
			# We may be copying a repo that can't run "git
			# status" due to a locked index. Since we have
			# a copy it's fine to remove the lock.
			rm .git/index.lock
		fi &&
		if test_bool_env GIT_PERF_USE_SCALAR false
		then
			"$MODERN_SCALAR" register
		fi
	) || error "failed to copy repository '$source' to '$repo'"
}

# call at least one of these to establish an appropriately-sized repository
test_perf_fresh_repo () {
	repo="${1:-$TRASH_DIRECTORY}"
	"$MODERN_GIT" init -q "$repo" &&
	(
		cd "$repo" &&
		test_perf_do_repo_symlink_config_ &&
		if test_bool_env GIT_PERF_USE_SCALAR false
		then
			"$MODERN_SCALAR" register
		fi
	)
}

test_perf_default_repo () {
	test_perf_create_repo_from "${1:-$TRASH_DIRECTORY}" "$GIT_PERF_REPO"
}
test_perf_large_repo () {
	if test "$GIT_PERF_LARGE_REPO" = "$GIT_BUILD_DIR"; then
		echo "warning: \$GIT_PERF_LARGE_REPO is \$GIT_BUILD_DIR." >&2
		echo "warning: This will work, but may not be a sufficiently large repo" >&2
		echo "warning: for representative measurements." >&2
	fi
	test_perf_create_repo_from "${1:-$TRASH_DIRECTORY}" "$GIT_PERF_LARGE_REPO"
}
test_checkout_worktree () {
	git checkout-index -u -a ||
	error "git checkout-index failed"
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
	local test_wrapper_func_="$1"; shift
	local test_title_="$1"; shift
	test_start_
	test_prereq=
	test_perf_setup_=
	while test $# != 0
	do
		case $1 in
		--prereq)
			test_prereq=$2
			shift
			;;
		--setup)
			test_perf_setup_=$2
			shift
			;;
		*)
			break
			;;
		esac
		shift
	done
	test "$#" = 1 || BUG "test_wrapper_ needs 2 positional parameters"
	export test_prereq
	export test_perf_setup_

	if ! test_skip "$test_title_" "$@"
	then
		base=$(basename "$0" .sh)
		echo "$test_count" >>"$perf_results_dir"/$base.subtests
		echo "$test_title_" >"$perf_results_dir"/$base.$test_count.descr
		base="$perf_results_dir"/"$PERF_RESULTS_PREFIX$(basename "$0" .sh)"."$test_count"
		"$test_wrapper_func_" "$test_title_" "$@"
	fi

	test_finish_
}

test_perf_ () {
	if test -z "$verbose"; then
		printf "%s" "perf $test_count - $1:"
	else
		echo "perf $test_count - $1:"
	fi
	for i in $(test_seq 1 $GIT_PERF_REPEAT_COUNT); do
		if test -n "$test_perf_setup_"
		then
			say >&3 "setup: $test_perf_setup_"
			if ! test_eval_ $test_perf_setup_
			then
				test_failure_ "$test_perf_setup_"
				break
			fi

		fi
		say >&3 "running: $2"
		if test_run_perf_ "$2"
		then
			if test -z "$verbose"; then
				printf " %s" "$i"
			else
				echo "* timing run $i/$GIT_PERF_REPEAT_COUNT:"
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

# Usage: test_perf 'title' [options] 'perf-test'
#	Run the performance test script specified in perf-test with
#	optional prerequisite and setup steps.
# Options:
#	--prereq prerequisites: Skip the test if prerequisites aren't met
#	--setup "setup-steps": Run setup steps prior to each measured iteration
#
test_perf () {
	test_wrapper_ test_perf_ "$@"
}

test_size_ () {
	if test -n "$test_perf_setup_"
	then
		say >&3 "setup: $test_perf_setup_"
		test_eval_ $test_perf_setup_
	fi

	say >&3 "running: $2"
	if test_eval_ "$2" 3>"$base".result; then
		test_ok_ "$1"
	else
		test_failure_ "$@"
	fi
}

# Usage: test_size 'title' [options] 'size-test'
#	Run the size test script specified in size-test with optional
#	prerequisites and setup steps. Returns the numeric value
#	returned by size-test.
# Options:
#	--prereq prerequisites: Skip the test if prerequisites aren't met
#	--setup "setup-steps": Run setup steps prior to the size measurement

test_size () {
	test_wrapper_ test_size_ "$@"
}

# We extend test_done to print timings at the end (./run disables this
# and does it after running everything)
test_at_end_hook_ () {
	if test -z "$GIT_PERF_AGGREGATING_LATER"; then
		(
			cd "$TEST_DIRECTORY"/perf &&
			./aggregate.perl --results-dir="$TEST_RESULTS_DIR" $(basename "$0")
		)
	fi
}

test_export () {
	export "$@"
}

test_lazy_prereq PERF_EXTRA 'test_bool_env GIT_PERF_EXTRA false'
