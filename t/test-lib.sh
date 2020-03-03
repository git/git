# Test framework for git.  See t/README for usage.
#
# Copyright (c) 2005 Junio C Hamano
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

# Test the binaries we have just built.  The tests are kept in
# t/ subdirectory and are run in 'trash directory' subdirectory.
if test -z "$TEST_DIRECTORY"
then
	# We allow tests to override this, in case they want to run tests
	# outside of t/, e.g. for running tests on the test library
	# itself.
	TEST_DIRECTORY=$(pwd)
else
	# ensure that TEST_DIRECTORY is an absolute path so that it
	# is valid even if the current working directory is changed
	TEST_DIRECTORY=$(cd "$TEST_DIRECTORY" && pwd) || exit 1
fi
if test -z "$TEST_OUTPUT_DIRECTORY"
then
	# Similarly, override this to store the test-results subdir
	# elsewhere
	TEST_OUTPUT_DIRECTORY=$TEST_DIRECTORY
fi
GIT_BUILD_DIR="$TEST_DIRECTORY"/..

# If we were built with ASAN, it may complain about leaks
# of program-lifetime variables. Disable it by default to lower
# the noise level. This needs to happen at the start of the script,
# before we even do our "did we build git yet" check (since we don't
# want that one to complain to stderr).
: ${ASAN_OPTIONS=detect_leaks=0:abort_on_error=1}
export ASAN_OPTIONS

# If LSAN is in effect we _do_ want leak checking, but we still
# want to abort so that we notice the problems.
: ${LSAN_OPTIONS=abort_on_error=1}
export LSAN_OPTIONS

if test ! -f "$GIT_BUILD_DIR"/GIT-BUILD-OPTIONS
then
	echo >&2 'error: GIT-BUILD-OPTIONS missing (has Git been built?).'
	exit 1
fi
. "$GIT_BUILD_DIR"/GIT-BUILD-OPTIONS
export PERL_PATH SHELL_PATH

# Disallow the use of abbreviated options in the test suite by default
if test -z "${GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS}"
then
	GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=true
	export GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS
fi

################################################################
# It appears that people try to run tests without building...
"${GIT_TEST_INSTALLED:-$GIT_BUILD_DIR}/git$X" >/dev/null
if test $? != 1
then
	if test -n "$GIT_TEST_INSTALLED"
	then
		echo >&2 "error: there is no working Git at '$GIT_TEST_INSTALLED'"
	else
		echo >&2 'error: you do not seem to have built git yet.'
	fi
	exit 1
fi

# Parse options while taking care to leave $@ intact, so we will still
# have all the original command line options when executing the test
# script again for '--tee' and '--verbose-log' below.
store_arg_to=
prev_opt=
for opt
do
	if test -n "$store_arg_to"
	then
		eval $store_arg_to=\$opt
		store_arg_to=
		prev_opt=
		continue
	fi

	case "$opt" in
	-d|--d|--de|--deb|--debu|--debug)
		debug=t ;;
	-i|--i|--im|--imm|--imme|--immed|--immedi|--immedia|--immediat|--immediate)
		immediate=t ;;
	-l|--l|--lo|--lon|--long|--long-|--long-t|--long-te|--long-tes|--long-test|--long-tests)
		GIT_TEST_LONG=t; export GIT_TEST_LONG ;;
	-r)
		store_arg_to=run_list
		;;
	--run=*)
		run_list=${opt#--*=} ;;
	-h|--h|--he|--hel|--help)
		help=t ;;
	-v|--v|--ve|--ver|--verb|--verbo|--verbos|--verbose)
		verbose=t ;;
	--verbose-only=*)
		verbose_only=${opt#--*=}
		;;
	-q|--q|--qu|--qui|--quie|--quiet)
		# Ignore --quiet under a TAP::Harness. Saying how many tests
		# passed without the ok/not ok details is always an error.
		test -z "$HARNESS_ACTIVE" && quiet=t ;;
	--with-dashes)
		with_dashes=t ;;
	--no-bin-wrappers)
		no_bin_wrappers=t ;;
	--no-color)
		color= ;;
	--va|--val|--valg|--valgr|--valgri|--valgrin|--valgrind)
		valgrind=memcheck
		tee=t
		;;
	--valgrind=*)
		valgrind=${opt#--*=}
		tee=t
		;;
	--valgrind-only=*)
		valgrind_only=${opt#--*=}
		tee=t
		;;
	--tee)
		tee=t ;;
	--root=*)
		root=${opt#--*=} ;;
	--chain-lint)
		GIT_TEST_CHAIN_LINT=1 ;;
	--no-chain-lint)
		GIT_TEST_CHAIN_LINT=0 ;;
	-x)
		trace=t ;;
	-V|--verbose-log)
		verbose_log=t
		tee=t
		;;
	--write-junit-xml)
		write_junit_xml=t
		;;
	--stress)
		stress=t ;;
	--stress=*)
		echo "error: --stress does not accept an argument: '$opt'" >&2
		echo "did you mean --stress-jobs=${opt#*=} or --stress-limit=${opt#*=}?" >&2
		exit 1
		;;
	--stress-jobs=*)
		stress=t;
		stress=${opt#--*=}
		case "$stress" in
		*[!0-9]*|0*|"")
			echo "error: --stress-jobs=<N> requires the number of jobs to run" >&2
			exit 1
			;;
		*)	# Good.
			;;
		esac
		;;
	--stress-limit=*)
		stress=t;
		stress_limit=${opt#--*=}
		case "$stress_limit" in
		*[!0-9]*|0*|"")
			echo "error: --stress-limit=<N> requires the number of repetitions" >&2
			exit 1
			;;
		*)	# Good.
			;;
		esac
		;;
	*)
		echo "error: unknown test option '$opt'" >&2; exit 1 ;;
	esac

	prev_opt=$opt
done
if test -n "$store_arg_to"
then
	echo "error: $prev_opt requires an argument" >&2
	exit 1
fi

if test -n "$valgrind_only"
then
	test -z "$valgrind" && valgrind=memcheck
	test -z "$verbose" && verbose_only="$valgrind_only"
elif test -n "$valgrind"
then
	test -z "$verbose_log" && verbose=t
fi

if test -n "$stress"
then
	verbose=t
	trace=t
	immediate=t
fi

TEST_STRESS_JOB_SFX="${GIT_TEST_STRESS_JOB_NR:+.stress-$GIT_TEST_STRESS_JOB_NR}"
TEST_NAME="$(basename "$0" .sh)"
TEST_NUMBER="${TEST_NAME%%-*}"
TEST_NUMBER="${TEST_NUMBER#t}"
TEST_RESULTS_DIR="$TEST_OUTPUT_DIRECTORY/test-results"
TEST_RESULTS_BASE="$TEST_RESULTS_DIR/$TEST_NAME$TEST_STRESS_JOB_SFX"
TRASH_DIRECTORY="trash directory.$TEST_NAME$TEST_STRESS_JOB_SFX"
test -n "$root" && TRASH_DIRECTORY="$root/$TRASH_DIRECTORY"
case "$TRASH_DIRECTORY" in
/*) ;; # absolute path is good
 *) TRASH_DIRECTORY="$TEST_OUTPUT_DIRECTORY/$TRASH_DIRECTORY" ;;
esac

# If --stress was passed, run this test repeatedly in several parallel loops.
if test "$GIT_TEST_STRESS_STARTED" = "done"
then
	: # Don't stress test again.
elif test -n "$stress"
then
	if test "$stress" != t
	then
		job_count=$stress
	elif test -n "$GIT_TEST_STRESS_LOAD"
	then
		job_count="$GIT_TEST_STRESS_LOAD"
	elif job_count=$(getconf _NPROCESSORS_ONLN 2>/dev/null) &&
	     test -n "$job_count"
	then
		job_count=$((2 * $job_count))
	else
		job_count=8
	fi

	mkdir -p "$TEST_RESULTS_DIR"
	stressfail="$TEST_RESULTS_BASE.stress-failed"
	rm -f "$stressfail"

	stress_exit=0
	trap '
		kill $job_pids 2>/dev/null
		wait
		stress_exit=1
	' TERM INT HUP

	job_pids=
	job_nr=0
	while test $job_nr -lt "$job_count"
	do
		(
			GIT_TEST_STRESS_STARTED=done
			GIT_TEST_STRESS_JOB_NR=$job_nr
			export GIT_TEST_STRESS_STARTED GIT_TEST_STRESS_JOB_NR

			trap '
				kill $test_pid 2>/dev/null
				wait
				exit 1
			' TERM INT

			cnt=1
			while ! test -e "$stressfail" &&
			      { test -z "$stress_limit" ||
				test $cnt -le $stress_limit ; }
			do
				$TEST_SHELL_PATH "$0" "$@" >"$TEST_RESULTS_BASE.stress-$job_nr.out" 2>&1 &
				test_pid=$!

				if wait $test_pid
				then
					printf "OK   %2d.%d\n" $GIT_TEST_STRESS_JOB_NR $cnt
				else
					echo $GIT_TEST_STRESS_JOB_NR >>"$stressfail"
					printf "FAIL %2d.%d\n" $GIT_TEST_STRESS_JOB_NR $cnt
				fi
				cnt=$(($cnt + 1))
			done
		) &
		job_pids="$job_pids $!"
		job_nr=$(($job_nr + 1))
	done

	wait

	if test -f "$stressfail"
	then
		stress_exit=1
		echo "Log(s) of failed test run(s):"
		for failed_job_nr in $(sort -n "$stressfail")
		do
			echo "Contents of '$TEST_RESULTS_BASE.stress-$failed_job_nr.out':"
			cat "$TEST_RESULTS_BASE.stress-$failed_job_nr.out"
		done
		rm -rf "$TRASH_DIRECTORY.stress-failed"
		# Move the last one.
		mv "$TRASH_DIRECTORY.stress-$failed_job_nr" "$TRASH_DIRECTORY.stress-failed"
	fi

	exit $stress_exit
fi

# if --tee was passed, write the output not only to the terminal, but
# additionally to the file test-results/$BASENAME.out, too.
if test "$GIT_TEST_TEE_STARTED" = "done"
then
	: # do not redirect again
elif test -n "$tee"
then
	mkdir -p "$TEST_RESULTS_DIR"

	# Make this filename available to the sub-process in case it is using
	# --verbose-log.
	GIT_TEST_TEE_OUTPUT_FILE=$TEST_RESULTS_BASE.out
	export GIT_TEST_TEE_OUTPUT_FILE

	# Truncate before calling "tee -a" to get rid of the results
	# from any previous runs.
	>"$GIT_TEST_TEE_OUTPUT_FILE"

	(GIT_TEST_TEE_STARTED=done ${TEST_SHELL_PATH} "$0" "$@" 2>&1;
	 echo $? >"$TEST_RESULTS_BASE.exit") | tee -a "$GIT_TEST_TEE_OUTPUT_FILE"
	test "$(cat "$TEST_RESULTS_BASE.exit")" = 0
	exit
fi

if test -n "$trace" && test -n "$test_untraceable"
then
	# '-x' tracing requested, but this test script can't be reliably
	# traced, unless it is run with a Bash version supporting
	# BASH_XTRACEFD (introduced in Bash v4.1).
	#
	# Perform this version check _after_ the test script was
	# potentially re-executed with $TEST_SHELL_PATH for '--tee' or
	# '--verbose-log', so the right shell is checked and the
	# warning is issued only once.
	if test -n "$BASH_VERSION" && eval '
	     test ${BASH_VERSINFO[0]} -gt 4 || {
	       test ${BASH_VERSINFO[0]} -eq 4 &&
	       test ${BASH_VERSINFO[1]} -ge 1
	     }
	   '
	then
		: Executed by a Bash version supporting BASH_XTRACEFD.  Good.
	else
		echo >&2 "warning: ignoring -x; '$0' is untraceable without BASH_XTRACEFD"
		trace=
	fi
fi
if test -n "$trace" && test -z "$verbose_log"
then
	verbose=t
fi

# For repeatability, reset the environment to known value.
# TERM is sanitized below, after saving color control sequences.
LANG=C
LC_ALL=C
PAGER=cat
TZ=UTC
export LANG LC_ALL PAGER TZ
EDITOR=:

# GIT_TEST_GETTEXT_POISON should not influence git commands executed
# during initialization of test-lib and the test repo. Back it up,
# unset and then restore after initialization is finished.
if test -n "$GIT_TEST_GETTEXT_POISON"
then
	GIT_TEST_GETTEXT_POISON_ORIG=$GIT_TEST_GETTEXT_POISON
	unset GIT_TEST_GETTEXT_POISON
fi

# A call to "unset" with no arguments causes at least Solaris 10
# /usr/xpg4/bin/sh and /bin/ksh to bail out.  So keep the unsets
# deriving from the command substitution clustered with the other
# ones.
unset VISUAL EMAIL LANGUAGE COLUMNS $("$PERL_PATH" -e '
	my @env = keys %ENV;
	my $ok = join("|", qw(
		TRACE
		DEBUG
		TEST
		.*_TEST
		PROVE
		VALGRIND
		UNZIP
		PERF_
		CURL_VERBOSE
		TRACE_CURL
	));
	my @vars = grep(/^GIT_/ && !/^GIT_($ok)/o, @env);
	print join("\n", @vars);
')
unset XDG_CACHE_HOME
unset XDG_CONFIG_HOME
unset GITPERLLIB
TEST_AUTHOR_LOCALNAME=author
TEST_AUTHOR_DOMAIN=example.com
GIT_AUTHOR_EMAIL=${TEST_AUTHOR_LOCALNAME}@${TEST_AUTHOR_DOMAIN}
GIT_AUTHOR_NAME='A U Thor'
TEST_COMMITTER_LOCALNAME=committer
TEST_COMMITTER_DOMAIN=example.com
GIT_COMMITTER_EMAIL=${TEST_COMMITTER_LOCALNAME}@${TEST_COMMITTER_DOMAIN}
GIT_COMMITTER_NAME='C O Mitter'
GIT_MERGE_VERBOSITY=5
GIT_MERGE_AUTOEDIT=no
export GIT_MERGE_VERBOSITY GIT_MERGE_AUTOEDIT
export GIT_AUTHOR_EMAIL GIT_AUTHOR_NAME
export GIT_COMMITTER_EMAIL GIT_COMMITTER_NAME
export EDITOR

# Tests using GIT_TRACE typically don't want <timestamp> <file>:<line> output
GIT_TRACE_BARE=1
export GIT_TRACE_BARE

check_var_migration () {
	# the warnings and hints given from this helper depends
	# on end-user settings, which will disrupt the self-test
	# done on the test framework itself.
	case "$GIT_TEST_FRAMEWORK_SELFTEST" in
	t)	return ;;
	esac

	old_name=$1 new_name=$2
	eval "old_isset=\${${old_name}:+isset}"
	eval "new_isset=\${${new_name}:+isset}"

	case "$old_isset,$new_isset" in
	isset,)
		echo >&2 "warning: $old_name is now $new_name"
		echo >&2 "hint: set $new_name too during the transition period"
		eval "$new_name=\$$old_name"
		;;
	isset,isset)
		# do this later
		# echo >&2 "warning: $old_name is now $new_name"
		# echo >&2 "hint: remove $old_name"
		;;
	esac
}

check_var_migration GIT_FSMONITOR_TEST GIT_TEST_FSMONITOR
check_var_migration TEST_GIT_INDEX_VERSION GIT_TEST_INDEX_VERSION
check_var_migration GIT_FORCE_PRELOAD_TEST GIT_TEST_PRELOAD_INDEX

# Use specific version of the index file format
if test -n "${GIT_TEST_INDEX_VERSION:+isset}"
then
	GIT_INDEX_VERSION="$GIT_TEST_INDEX_VERSION"
	export GIT_INDEX_VERSION
fi

# Add libc MALLOC and MALLOC_PERTURB test
# only if we are not executing the test with valgrind
if test -n "$valgrind" ||
   test -n "$TEST_NO_MALLOC_CHECK"
then
	setup_malloc_check () {
		: nothing
	}
	teardown_malloc_check () {
		: nothing
	}
else
	setup_malloc_check () {
		MALLOC_CHECK_=3	MALLOC_PERTURB_=165
		export MALLOC_CHECK_ MALLOC_PERTURB_
	}
	teardown_malloc_check () {
		unset MALLOC_CHECK_ MALLOC_PERTURB_
	}
fi

# Protect ourselves from common misconfiguration to export
# CDPATH into the environment
unset CDPATH

unset GREP_OPTIONS
unset UNZIP

case $(echo $GIT_TRACE |tr "[A-Z]" "[a-z]") in
1|2|true)
	GIT_TRACE=4
	;;
esac

# Line feed
LF='
'

# Single quote
SQ=\'

# UTF-8 ZERO WIDTH NON-JOINER, which HFS+ ignores
# when case-folding filenames
u200c=$(printf '\342\200\214')

export _x05 _x35 _x40 _z40 LF u200c EMPTY_TREE EMPTY_BLOB ZERO_OID OID_REGEX

# Each test should start with something like this, after copyright notices:
#
# test_description='Description of this test...
# This test checks if command xyzzy does the right thing...
# '
# . ./test-lib.sh
test "x$TERM" != "xdumb" && (
		test -t 1 &&
		tput bold >/dev/null 2>&1 &&
		tput setaf 1 >/dev/null 2>&1 &&
		tput sgr0 >/dev/null 2>&1
	) &&
	color=t

if test -n "$color"
then
	# Save the color control sequences now rather than run tput
	# each time say_color() is called.  This is done for two
	# reasons:
	#   * TERM will be changed to dumb
	#   * HOME will be changed to a temporary directory and tput
	#     might need to read ~/.terminfo from the original HOME
	#     directory to get the control sequences
	# Note:  This approach assumes the control sequences don't end
	# in a newline for any terminal of interest (command
	# substitutions strip trailing newlines).  Given that most
	# (all?) terminals in common use are related to ECMA-48, this
	# shouldn't be a problem.
	say_color_error=$(tput bold; tput setaf 1) # bold red
	say_color_skip=$(tput setaf 4) # blue
	say_color_warn=$(tput setaf 3) # brown/yellow
	say_color_pass=$(tput setaf 2) # green
	say_color_info=$(tput setaf 6) # cyan
	say_color_reset=$(tput sgr0)
	say_color_="" # no formatting for normal text
	say_color () {
		test -z "$1" && test -n "$quiet" && return
		eval "say_color_color=\$say_color_$1"
		shift
		printf "%s\\n" "$say_color_color$*$say_color_reset"
	}
else
	say_color() {
		test -z "$1" && test -n "$quiet" && return
		shift
		printf "%s\n" "$*"
	}
fi

TERM=dumb
export TERM

error () {
	say_color error "error: $*"
	finalize_junit_xml
	GIT_EXIT_OK=t
	exit 1
}

BUG () {
	error >&7 "bug in the test script: $*"
}

say () {
	say_color info "$*"
}

if test -n "$HARNESS_ACTIVE"
then
	if test "$verbose" = t || test -n "$verbose_only"
	then
		printf 'Bail out! %s\n' \
		 'verbose mode forbidden under TAP harness; try --verbose-log'
		exit 1
	fi
fi

test "${test_description}" != "" ||
error "Test script did not set test_description."

if test "$help" = "t"
then
	printf '%s\n' "$test_description"
	exit 0
fi

exec 5>&1
exec 6<&0
exec 7>&2
if test "$verbose_log" = "t"
then
	exec 3>>"$GIT_TEST_TEE_OUTPUT_FILE" 4>&3
elif test "$verbose" = "t"
then
	exec 4>&2 3>&1
else
	exec 4>/dev/null 3>/dev/null
fi

# Send any "-x" output directly to stderr to avoid polluting tests
# which capture stderr. We can do this unconditionally since it
# has no effect if tracing isn't turned on.
#
# Note that this sets up the trace fd as soon as we assign the variable, so it
# must come after the creation of descriptor 4 above. Likewise, we must never
# unset this, as it has the side effect of closing descriptor 4, which we
# use to show verbose tests to the user.
#
# Note also that we don't need or want to export it. The tracing is local to
# this shell, and we would not want to influence any shells we exec.
BASH_XTRACEFD=4

test_failure=0
test_count=0
test_fixed=0
test_broken=0
test_success=0

test_external_has_tap=0

die () {
	code=$?
	# This is responsible for running the atexit commands even when a
	# test script run with '--immediate' fails, or when the user hits
	# ctrl-C, i.e. when 'test_done' is not invoked at all.
	test_atexit_handler || code=$?
	if test -n "$GIT_EXIT_OK"
	then
		exit $code
	else
		echo >&5 "FATAL: Unexpected exit with code $code"
		exit 1
	fi
}

GIT_EXIT_OK=
trap 'die' EXIT
# Disable '-x' tracing, because with some shells, notably dash, it
# prevents running the cleanup commands when a test script run with
# '--verbose-log -x' is interrupted.
trap '{ code=$?; set +x; } 2>/dev/null; exit $code' INT TERM HUP

# The user-facing functions are loaded from a separate file so that
# test_perf subshells can have them too
. "$TEST_DIRECTORY/test-lib-functions.sh"

# You are not expected to call test_ok_ and test_failure_ directly, use
# the test_expect_* functions instead.

test_ok_ () {
	if test -n "$write_junit_xml"
	then
		write_junit_xml_testcase "$*"
	fi
	test_success=$(($test_success + 1))
	say_color "" "ok $test_count - $@"
}

test_failure_ () {
	if test -n "$write_junit_xml"
	then
		junit_insert="<failure message=\"not ok $test_count -"
		junit_insert="$junit_insert $(xml_attr_encode "$1")\">"
		junit_insert="$junit_insert $(xml_attr_encode \
			"$(if test -n "$GIT_TEST_TEE_OUTPUT_FILE"
			   then
				test-tool path-utils skip-n-bytes \
					"$GIT_TEST_TEE_OUTPUT_FILE" $GIT_TEST_TEE_OFFSET
			   else
				printf '%s\n' "$@" | sed 1d
			   fi)")"
		junit_insert="$junit_insert</failure>"
		if test -n "$GIT_TEST_TEE_OUTPUT_FILE"
		then
			junit_insert="$junit_insert<system-err>$(xml_attr_encode \
				"$(cat "$GIT_TEST_TEE_OUTPUT_FILE")")</system-err>"
		fi
		write_junit_xml_testcase "$1" "      $junit_insert"
	fi
	test_failure=$(($test_failure + 1))
	say_color error "not ok $test_count - $1"
	shift
	printf '%s\n' "$*" | sed -e 's/^/#	/'
	test "$immediate" = "" || { finalize_junit_xml; GIT_EXIT_OK=t; exit 1; }
}

test_known_broken_ok_ () {
	if test -n "$write_junit_xml"
	then
		write_junit_xml_testcase "$* (breakage fixed)"
	fi
	test_fixed=$(($test_fixed+1))
	say_color error "ok $test_count - $@ # TODO known breakage vanished"
}

test_known_broken_failure_ () {
	if test -n "$write_junit_xml"
	then
		write_junit_xml_testcase "$* (known breakage)"
	fi
	test_broken=$(($test_broken+1))
	say_color warn "not ok $test_count - $@ # TODO known breakage"
}

test_debug () {
	test "$debug" = "" || eval "$1"
}

match_pattern_list () {
	arg="$1"
	shift
	test -z "$*" && return 1
	for pattern_
	do
		case "$arg" in
		$pattern_)
			return 0
		esac
	done
	return 1
}

match_test_selector_list () {
	title="$1"
	shift
	arg="$1"
	shift
	test -z "$1" && return 0

	# Both commas and whitespace are accepted as separators.
	OLDIFS=$IFS
	IFS=' 	,'
	set -- $1
	IFS=$OLDIFS

	# If the first selector is negative we include by default.
	include=
	case "$1" in
		!*) include=t ;;
	esac

	for selector
	do
		orig_selector=$selector

		positive=t
		case "$selector" in
			!*)
				positive=
				selector=${selector##?}
				;;
		esac

		test -z "$selector" && continue

		case "$selector" in
			*-*)
				if expr "z${selector%%-*}" : "z[0-9]*[^0-9]" >/dev/null
				then
					echo "error: $title: invalid non-numeric in range" \
						"start: '$orig_selector'" >&2
					exit 1
				fi
				if expr "z${selector#*-}" : "z[0-9]*[^0-9]" >/dev/null
				then
					echo "error: $title: invalid non-numeric in range" \
						"end: '$orig_selector'" >&2
					exit 1
				fi
				;;
			*)
				if expr "z$selector" : "z[0-9]*[^0-9]" >/dev/null
				then
					echo "error: $title: invalid non-numeric in test" \
						"selector: '$orig_selector'" >&2
					exit 1
				fi
		esac

		# Short cut for "obvious" cases
		test -z "$include" && test -z "$positive" && continue
		test -n "$include" && test -n "$positive" && continue

		case "$selector" in
			-*)
				if test $arg -le ${selector#-}
				then
					include=$positive
				fi
				;;
			*-)
				if test $arg -ge ${selector%-}
				then
					include=$positive
				fi
				;;
			*-*)
				if test ${selector%%-*} -le $arg \
					&& test $arg -le ${selector#*-}
				then
					include=$positive
				fi
				;;
			*)
				if test $arg -eq $selector
				then
					include=$positive
				fi
				;;
		esac
	done

	test -n "$include"
}

maybe_teardown_verbose () {
	test -z "$verbose_only" && return
	exec 4>/dev/null 3>/dev/null
	verbose=
}

last_verbose=t
maybe_setup_verbose () {
	test -z "$verbose_only" && return
	if match_pattern_list $test_count $verbose_only
	then
		exec 4>&2 3>&1
		# Emit a delimiting blank line when going from
		# non-verbose to verbose.  Within verbose mode the
		# delimiter is printed by test_expect_*.  The choice
		# of the initial $last_verbose is such that before
		# test 1, we do not print it.
		test -z "$last_verbose" && echo >&3 ""
		verbose=t
	else
		exec 4>/dev/null 3>/dev/null
		verbose=
	fi
	last_verbose=$verbose
}

maybe_teardown_valgrind () {
	test -z "$GIT_VALGRIND" && return
	GIT_VALGRIND_ENABLED=
}

maybe_setup_valgrind () {
	test -z "$GIT_VALGRIND" && return
	if test -z "$valgrind_only"
	then
		GIT_VALGRIND_ENABLED=t
		return
	fi
	GIT_VALGRIND_ENABLED=
	if match_pattern_list $test_count $valgrind_only
	then
		GIT_VALGRIND_ENABLED=t
	fi
}

want_trace () {
	test "$trace" = t && {
		test "$verbose" = t || test "$verbose_log" = t
	}
}

# This is a separate function because some tests use
# "return" to end a test_expect_success block early
# (and we want to make sure we run any cleanup like
# "set +x").
test_eval_inner_ () {
	# Do not add anything extra (including LF) after '$*'
	eval "
		want_trace && set -x
		$*"
}

test_eval_ () {
	# If "-x" tracing is in effect, then we want to avoid polluting stderr
	# with non-test commands. But once in "set -x" mode, we cannot prevent
	# the shell from printing the "set +x" to turn it off (nor the saving
	# of $? before that). But we can make sure that the output goes to
	# /dev/null.
	#
	# There are a few subtleties here:
	#
	#   - we have to redirect descriptor 4 in addition to 2, to cover
	#     BASH_XTRACEFD
	#
	#   - the actual eval has to come before the redirection block (since
	#     it needs to see descriptor 4 to set up its stderr)
	#
	#   - likewise, any error message we print must be outside the block to
	#     access descriptor 4
	#
	#   - checking $? has to come immediately after the eval, but it must
	#     be _inside_ the block to avoid polluting the "set -x" output
	#

	test_eval_inner_ "$@" </dev/null >&3 2>&4
	{
		test_eval_ret_=$?
		if want_trace
		then
			set +x
		fi
	} 2>/dev/null 4>&2

	if test "$test_eval_ret_" != 0 && want_trace
	then
		say_color error >&4 "error: last command exited with \$?=$test_eval_ret_"
	fi
	return $test_eval_ret_
}

test_run_ () {
	test_cleanup=:
	expecting_failure=$2

	if test "${GIT_TEST_CHAIN_LINT:-1}" != 0; then
		# turn off tracing for this test-eval, as it simply creates
		# confusing noise in the "-x" output
		trace_tmp=$trace
		trace=
		# 117 is magic because it is unlikely to match the exit
		# code of other programs
		if $(printf '%s\n' "$1" | sed -f "$GIT_BUILD_DIR/t/chainlint.sed" | grep -q '?![A-Z][A-Z]*?!') ||
			test "OK-117" != "$(test_eval_ "(exit 117) && $1${LF}${LF}echo OK-\$?" 3>&1)"
		then
			BUG "broken &&-chain or run-away HERE-DOC: $1"
		fi
		trace=$trace_tmp
	fi

	setup_malloc_check
	test_eval_ "$1"
	eval_ret=$?
	teardown_malloc_check

	if test -z "$immediate" || test $eval_ret = 0 ||
	   test -n "$expecting_failure" && test "$test_cleanup" != ":"
	then
		setup_malloc_check
		test_eval_ "$test_cleanup"
		teardown_malloc_check
	fi
	if test "$verbose" = "t" && test -n "$HARNESS_ACTIVE"
	then
		echo ""
	fi
	return "$eval_ret"
}

test_start_ () {
	test_count=$(($test_count+1))
	maybe_setup_verbose
	maybe_setup_valgrind
	if test -n "$write_junit_xml"
	then
		junit_start=$(test-tool date getnanos)
	fi
}

test_finish_ () {
	echo >&3 ""
	maybe_teardown_valgrind
	maybe_teardown_verbose
	if test -n "$GIT_TEST_TEE_OFFSET"
	then
		GIT_TEST_TEE_OFFSET=$(test-tool path-utils file-size \
			"$GIT_TEST_TEE_OUTPUT_FILE")
	fi
}

test_skip () {
	to_skip=
	skipped_reason=
	if match_pattern_list $this_test.$test_count $GIT_SKIP_TESTS
	then
		to_skip=t
		skipped_reason="GIT_SKIP_TESTS"
	fi
	if test -z "$to_skip" && test -n "$run_list" &&
	   ! match_test_selector_list '--run' $test_count "$run_list"
	then
		to_skip=t
		skipped_reason="--run"
	fi
	if test -z "$to_skip" && test -n "$test_prereq" &&
	   ! test_have_prereq "$test_prereq"
	then
		to_skip=t

		of_prereq=
		if test "$missing_prereq" != "$test_prereq"
		then
			of_prereq=" of $test_prereq"
		fi
		skipped_reason="missing $missing_prereq${of_prereq}"
	fi

	case "$to_skip" in
	t)
		if test -n "$write_junit_xml"
		then
			message="$(xml_attr_encode "$skipped_reason")"
			write_junit_xml_testcase "$1" \
				"      <skipped message=\"$message\" />"
		fi

		say_color skip >&3 "skipping test: $@"
		say_color skip "ok $test_count # skip $1 ($skipped_reason)"
		: true
		;;
	*)
		false
		;;
	esac
}

# stub; perf-lib overrides it
test_at_end_hook_ () {
	:
}

write_junit_xml () {
	case "$1" in
	--truncate)
		>"$junit_xml_path"
		junit_have_testcase=
		shift
		;;
	esac
	printf '%s\n' "$@" >>"$junit_xml_path"
}

xml_attr_encode () {
	printf '%s\n' "$@" | test-tool xml-encode
}

write_junit_xml_testcase () {
	junit_attrs="name=\"$(xml_attr_encode "$this_test.$test_count $1")\""
	shift
	junit_attrs="$junit_attrs classname=\"$this_test\""
	junit_attrs="$junit_attrs time=\"$(test-tool \
		date getnanos $junit_start)\""
	write_junit_xml "$(printf '%s\n' \
		"    <testcase $junit_attrs>" "$@" "    </testcase>")"
	junit_have_testcase=t
}

finalize_junit_xml () {
	if test -n "$write_junit_xml" && test -n "$junit_xml_path"
	then
		test -n "$junit_have_testcase" || {
			junit_start=$(test-tool date getnanos)
			write_junit_xml_testcase "all tests skipped"
		}

		# adjust the overall time
		junit_time=$(test-tool date getnanos $junit_suite_start)
		sed -e "s/\(<testsuite.*\) time=\"[^\"]*\"/\1/" \
			-e "s/<testsuite [^>]*/& time=\"$junit_time\"/" \
			<"$junit_xml_path" >"$junit_xml_path.new"
		mv "$junit_xml_path.new" "$junit_xml_path"

		write_junit_xml "  </testsuite>" "</testsuites>"
		write_junit_xml=
	fi
}

test_atexit_cleanup=:
test_atexit_handler () {
	# In a succeeding test script 'test_atexit_handler' is invoked
	# twice: first from 'test_done', then from 'die' in the trap on
	# EXIT.
	# This condition and resetting 'test_atexit_cleanup' below makes
	# sure that the registered cleanup commands are run only once.
	test : != "$test_atexit_cleanup" || return 0

	setup_malloc_check
	test_eval_ "$test_atexit_cleanup"
	test_atexit_cleanup=:
	teardown_malloc_check
}

test_done () {
	GIT_EXIT_OK=t

	# Run the atexit commands _before_ the trash directory is
	# removed, so the commands can access pidfiles and socket files.
	test_atexit_handler

	finalize_junit_xml

	if test -z "$HARNESS_ACTIVE"
	then
		mkdir -p "$TEST_RESULTS_DIR"

		cat >"$TEST_RESULTS_BASE.counts" <<-EOF
		total $test_count
		success $test_success
		fixed $test_fixed
		broken $test_broken
		failed $test_failure

		EOF
	fi

	if test "$test_fixed" != 0
	then
		say_color error "# $test_fixed known breakage(s) vanished; please update test(s)"
	fi
	if test "$test_broken" != 0
	then
		say_color warn "# still have $test_broken known breakage(s)"
	fi
	if test "$test_broken" != 0 || test "$test_fixed" != 0
	then
		test_remaining=$(( $test_count - $test_broken - $test_fixed ))
		msg="remaining $test_remaining test(s)"
	else
		test_remaining=$test_count
		msg="$test_count test(s)"
	fi
	case "$test_failure" in
	0)
		if test $test_external_has_tap -eq 0
		then
			if test $test_remaining -gt 0
			then
				say_color pass "# passed all $msg"
			fi

			# Maybe print SKIP message
			test -z "$skip_all" || skip_all="# SKIP $skip_all"
			case "$test_count" in
			0)
				say "1..$test_count${skip_all:+ $skip_all}"
				;;
			*)
				test -z "$skip_all" ||
				say_color warn "$skip_all"
				say "1..$test_count"
				;;
			esac
		fi

		if test -z "$debug"
		then
			test -d "$TRASH_DIRECTORY" ||
			error "Tests passed but trash directory already removed before test cleanup; aborting"

			cd "$TRASH_DIRECTORY/.." &&
			rm -fr "$TRASH_DIRECTORY" || {
				# try again in a bit
				sleep 5;
				rm -fr "$TRASH_DIRECTORY"
			} ||
			error "Tests passed but test cleanup failed; aborting"
		fi
		test_at_end_hook_

		exit 0 ;;

	*)
		if test $test_external_has_tap -eq 0
		then
			say_color error "# failed $test_failure among $msg"
			say "1..$test_count"
		fi

		exit 1 ;;

	esac
}

if test -n "$valgrind"
then
	make_symlink () {
		test -h "$2" &&
		test "$1" = "$(readlink "$2")" || {
			# be super paranoid
			if mkdir "$2".lock
			then
				rm -f "$2" &&
				ln -s "$1" "$2" &&
				rm -r "$2".lock
			else
				while test -d "$2".lock
				do
					say "Waiting for lock on $2."
					sleep 1
				done
			fi
		}
	}

	make_valgrind_symlink () {
		# handle only executables, unless they are shell libraries that
		# need to be in the exec-path.
		test -x "$1" ||
		test "# " = "$(test_copy_bytes 2 <"$1")" ||
		return;

		base=$(basename "$1")
		case "$base" in
		test-*)
			symlink_target="$GIT_BUILD_DIR/t/helper/$base"
			;;
		*)
			symlink_target="$GIT_BUILD_DIR/$base"
			;;
		esac
		# do not override scripts
		if test -x "$symlink_target" &&
		    test ! -d "$symlink_target" &&
		    test "#!" != "$(test_copy_bytes 2 <"$symlink_target")"
		then
			symlink_target=../valgrind.sh
		fi
		case "$base" in
		*.sh|*.perl)
			symlink_target=../unprocessed-script
		esac
		# create the link, or replace it if it is out of date
		make_symlink "$symlink_target" "$GIT_VALGRIND/bin/$base" || exit
	}

	# override all git executables in TEST_DIRECTORY/..
	GIT_VALGRIND=$TEST_DIRECTORY/valgrind
	mkdir -p "$GIT_VALGRIND"/bin
	for file in $GIT_BUILD_DIR/git* $GIT_BUILD_DIR/t/helper/test-*
	do
		make_valgrind_symlink $file
	done
	# special-case the mergetools loadables
	make_symlink "$GIT_BUILD_DIR"/mergetools "$GIT_VALGRIND/bin/mergetools"
	OLDIFS=$IFS
	IFS=:
	for path in $PATH
	do
		ls "$path"/git-* 2> /dev/null |
		while read file
		do
			make_valgrind_symlink "$file"
		done
	done
	IFS=$OLDIFS
	PATH=$GIT_VALGRIND/bin:$PATH
	GIT_EXEC_PATH=$GIT_VALGRIND/bin
	export GIT_VALGRIND
	GIT_VALGRIND_MODE="$valgrind"
	export GIT_VALGRIND_MODE
	GIT_VALGRIND_ENABLED=t
	test -n "$valgrind_only" && GIT_VALGRIND_ENABLED=
	export GIT_VALGRIND_ENABLED
elif test -n "$GIT_TEST_INSTALLED"
then
	GIT_EXEC_PATH=$($GIT_TEST_INSTALLED/git --exec-path)  ||
	error "Cannot run git from $GIT_TEST_INSTALLED."
	PATH=$GIT_TEST_INSTALLED:$GIT_BUILD_DIR/t/helper:$PATH
	GIT_EXEC_PATH=${GIT_TEST_EXEC_PATH:-$GIT_EXEC_PATH}
else # normal case, use ../bin-wrappers only unless $with_dashes:
	if test -n "$no_bin_wrappers"
	then
		with_dashes=t
	else
		git_bin_dir="$GIT_BUILD_DIR/bin-wrappers"
		if ! test -x "$git_bin_dir/git"
		then
			if test -z "$with_dashes"
			then
				say "$git_bin_dir/git is not executable; using GIT_EXEC_PATH"
			fi
			with_dashes=t
		fi
		PATH="$git_bin_dir:$PATH"
	fi
	GIT_EXEC_PATH=$GIT_BUILD_DIR
	if test -n "$with_dashes"
	then
		PATH="$GIT_BUILD_DIR:$GIT_BUILD_DIR/t/helper:$PATH"
	fi
fi
GIT_TEMPLATE_DIR="$GIT_BUILD_DIR"/templates/blt
GIT_CONFIG_NOSYSTEM=1
GIT_ATTR_NOSYSTEM=1
export PATH GIT_EXEC_PATH GIT_TEMPLATE_DIR GIT_CONFIG_NOSYSTEM GIT_ATTR_NOSYSTEM

if test -z "$GIT_TEST_CMP"
then
	if test -n "$GIT_TEST_CMP_USE_COPIED_CONTEXT"
	then
		GIT_TEST_CMP="$DIFF -c"
	else
		GIT_TEST_CMP="$DIFF -u"
	fi
fi

GITPERLLIB="$GIT_BUILD_DIR"/perl/build/lib
export GITPERLLIB
test -d "$GIT_BUILD_DIR"/templates/blt || {
	error "You haven't built things yet, have you?"
}

if ! test -x "$GIT_BUILD_DIR"/t/helper/test-tool$X
then
	echo >&2 'You need to build test-tool:'
	echo >&2 'Run "make t/helper/test-tool" in the source (toplevel) directory'
	exit 1
fi

# Test repository
rm -fr "$TRASH_DIRECTORY" || {
	GIT_EXIT_OK=t
	echo >&5 "FATAL: Cannot prepare test area"
	exit 1
}

HOME="$TRASH_DIRECTORY"
GNUPGHOME="$HOME/gnupg-home-not-used"
export HOME GNUPGHOME

if test -z "$TEST_NO_CREATE_REPO"
then
	test_create_repo "$TRASH_DIRECTORY"
else
	mkdir -p "$TRASH_DIRECTORY"
fi

# Use -P to resolve symlinks in our working directory so that the cwd
# in subprocesses like git equals our $PWD (for pathname comparisons).
cd -P "$TRASH_DIRECTORY" || exit 1

this_test=${0##*/}
this_test=${this_test%%-*}
if match_pattern_list "$this_test" $GIT_SKIP_TESTS
then
	say_color info >&3 "skipping test $this_test altogether"
	skip_all="skip all tests in $this_test"
	test_done
fi

if test -n "$write_junit_xml"
then
	junit_xml_dir="$TEST_OUTPUT_DIRECTORY/out"
	mkdir -p "$junit_xml_dir"
	junit_xml_base=${0##*/}
	junit_xml_path="$junit_xml_dir/TEST-${junit_xml_base%.sh}.xml"
	junit_attrs="name=\"${junit_xml_base%.sh}\""
	junit_attrs="$junit_attrs timestamp=\"$(TZ=UTC \
		date +%Y-%m-%dT%H:%M:%S)\""
	write_junit_xml --truncate "<testsuites>" "  <testsuite $junit_attrs>"
	junit_suite_start=$(test-tool date getnanos)
	if test -n "$GIT_TEST_TEE_OUTPUT_FILE"
	then
		GIT_TEST_TEE_OFFSET=0
	fi
fi

# Convenience
# A regexp to match 5, 35 and 40 hexdigits
_x05='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x35="$_x05$_x05$_x05$_x05$_x05$_x05$_x05"
_x40="$_x35$_x05"

test_oid_init

ZERO_OID=$(test_oid zero)
OID_REGEX=$(echo $ZERO_OID | sed -e 's/0/[0-9a-f]/g')
EMPTY_TREE=$(test_oid empty_tree)
EMPTY_BLOB=$(test_oid empty_blob)
_z40=$ZERO_OID

# Provide an implementation of the 'yes' utility; the upper bound
# limit is there to help Windows that cannot stop this loop from
# wasting cycles when the downstream stops reading, so do not be
# tempted to turn it into an infinite loop. cf. 6129c930 ("test-lib:
# limit the output of the yes utility", 2016-02-02)
yes () {
	if test $# = 0
	then
		y=y
	else
		y="$*"
	fi

	i=0
	while test $i -lt 99
	do
		echo "$y"
		i=$(($i+1))
	done
}

# The GIT_TEST_FAIL_PREREQS code hooks into test_set_prereq(), and
# thus needs to be set up really early, and set an internal variable
# for convenience so the hot test_set_prereq() codepath doesn't need
# to call "git env--helper" (via test_bool_env). Only do that work
# if needed by seeing if GIT_TEST_FAIL_PREREQS is set at all.
GIT_TEST_FAIL_PREREQS_INTERNAL=
if test -n "$GIT_TEST_FAIL_PREREQS"
then
	if test_bool_env GIT_TEST_FAIL_PREREQS false
	then
		GIT_TEST_FAIL_PREREQS_INTERNAL=true
		test_set_prereq FAIL_PREREQS
	fi
else
	test_lazy_prereq FAIL_PREREQS '
		test_bool_env GIT_TEST_FAIL_PREREQS false
	'
fi

# Fix some commands on Windows, and other OS-specific things
uname_s=$(uname -s)
case $uname_s in
*MINGW*)
	# Windows has its own (incompatible) sort and find
	sort () {
		/usr/bin/sort "$@"
	}
	find () {
		/usr/bin/find "$@"
	}
	# git sees Windows-style pwd
	pwd () {
		builtin pwd -W
	}
	# no POSIX permissions
	# backslashes in pathspec are converted to '/'
	# exec does not inherit the PID
	test_set_prereq MINGW
	test_set_prereq NATIVE_CRLF
	test_set_prereq SED_STRIPS_CR
	test_set_prereq GREP_STRIPS_CR
	GIT_TEST_CMP=mingw_test_cmp
	;;
*CYGWIN*)
	test_set_prereq POSIXPERM
	test_set_prereq EXECKEEPSPID
	test_set_prereq CYGWIN
	test_set_prereq SED_STRIPS_CR
	test_set_prereq GREP_STRIPS_CR
	;;
FreeBSD)
	test_set_prereq REGEX_ILLSEQ
	test_set_prereq POSIXPERM
	test_set_prereq BSLASHPSPEC
	test_set_prereq EXECKEEPSPID
	;;
*)
	test_set_prereq POSIXPERM
	test_set_prereq BSLASHPSPEC
	test_set_prereq EXECKEEPSPID
	;;
esac

( COLUMNS=1 && test $COLUMNS = 1 ) && test_set_prereq COLUMNS_CAN_BE_1
test -z "$NO_PERL" && test_set_prereq PERL
test -z "$NO_PTHREADS" && test_set_prereq PTHREADS
test -z "$NO_PYTHON" && test_set_prereq PYTHON
test -n "$USE_LIBPCRE1$USE_LIBPCRE2" && test_set_prereq PCRE
test -n "$USE_LIBPCRE1" && test_set_prereq LIBPCRE1
test -n "$USE_LIBPCRE2" && test_set_prereq LIBPCRE2
test -z "$NO_GETTEXT" && test_set_prereq GETTEXT

if test -n "$GIT_TEST_GETTEXT_POISON_ORIG"
then
	GIT_TEST_GETTEXT_POISON=$GIT_TEST_GETTEXT_POISON_ORIG
	export GIT_TEST_GETTEXT_POISON
	unset GIT_TEST_GETTEXT_POISON_ORIG
fi

test_lazy_prereq C_LOCALE_OUTPUT '
	! test_bool_env GIT_TEST_GETTEXT_POISON false
'

if test -z "$GIT_TEST_CHECK_CACHE_TREE"
then
	GIT_TEST_CHECK_CACHE_TREE=true
	export GIT_TEST_CHECK_CACHE_TREE
fi

test_lazy_prereq PIPE '
	# test whether the filesystem supports FIFOs
	test_have_prereq !MINGW,!CYGWIN &&
	rm -f testfifo && mkfifo testfifo
'

test_lazy_prereq SYMLINKS '
	# test whether the filesystem supports symbolic links
	ln -s x y && test -h y
'

test_lazy_prereq FILEMODE '
	test "$(git config --bool core.filemode)" = true
'

test_lazy_prereq CASE_INSENSITIVE_FS '
	echo good >CamelCase &&
	echo bad >camelcase &&
	test "$(cat CamelCase)" != good
'

test_lazy_prereq FUNNYNAMES '
	test_have_prereq !MINGW &&
	touch -- \
		"FUNNYNAMES tab	embedded" \
		"FUNNYNAMES \"quote embedded\"" \
		"FUNNYNAMES newline
embedded" 2>/dev/null &&
	rm -- \
		"FUNNYNAMES tab	embedded" \
		"FUNNYNAMES \"quote embedded\"" \
		"FUNNYNAMES newline
embedded" 2>/dev/null
'

test_lazy_prereq UTF8_NFD_TO_NFC '
	# check whether FS converts nfd unicode to nfc
	auml=$(printf "\303\244")
	aumlcdiar=$(printf "\141\314\210")
	>"$auml" &&
	test -f "$aumlcdiar"
'

test_lazy_prereq AUTOIDENT '
	sane_unset GIT_AUTHOR_NAME &&
	sane_unset GIT_AUTHOR_EMAIL &&
	git var GIT_AUTHOR_IDENT
'

test_lazy_prereq EXPENSIVE '
	test -n "$GIT_TEST_LONG"
'

test_lazy_prereq EXPENSIVE_ON_WINDOWS '
	test_have_prereq EXPENSIVE || test_have_prereq !MINGW,!CYGWIN
'

test_lazy_prereq USR_BIN_TIME '
	test -x /usr/bin/time
'

test_lazy_prereq NOT_ROOT '
	uid=$(id -u) &&
	test "$uid" != 0
'

test_lazy_prereq JGIT '
	jgit --version
'

# SANITY is about "can you correctly predict what the filesystem would
# do by only looking at the permission bits of the files and
# directories?"  A typical example of !SANITY is running the test
# suite as root, where a test may expect "chmod -r file && cat file"
# to fail because file is supposed to be unreadable after a successful
# chmod.  In an environment (i.e. combination of what filesystem is
# being used and who is running the tests) that lacks SANITY, you may
# be able to delete or create a file when the containing directory
# doesn't have write permissions, or access a file even if the
# containing directory doesn't have read or execute permissions.

test_lazy_prereq SANITY '
	mkdir SANETESTD.1 SANETESTD.2 &&

	chmod +w SANETESTD.1 SANETESTD.2 &&
	>SANETESTD.1/x 2>SANETESTD.2/x &&
	chmod -w SANETESTD.1 &&
	chmod -r SANETESTD.1/x &&
	chmod -rx SANETESTD.2 ||
	BUG "cannot prepare SANETESTD"

	! test -r SANETESTD.1/x &&
	! rm SANETESTD.1/x && ! test -f SANETESTD.2/x
	status=$?

	chmod +rwx SANETESTD.1 SANETESTD.2 &&
	rm -rf SANETESTD.1 SANETESTD.2 ||
	BUG "cannot clean SANETESTD"
	return $status
'

test FreeBSD != $uname_s || GIT_UNZIP=${GIT_UNZIP:-/usr/local/bin/unzip}
GIT_UNZIP=${GIT_UNZIP:-unzip}
test_lazy_prereq UNZIP '
	"$GIT_UNZIP" -v
	test $? -ne 127
'

run_with_limited_cmdline () {
	(ulimit -s 128 && "$@")
}

test_lazy_prereq CMDLINE_LIMIT '
	test_have_prereq !MINGW,!CYGWIN &&
	run_with_limited_cmdline true
'

run_with_limited_stack () {
	(ulimit -s 128 && "$@")
}

test_lazy_prereq ULIMIT_STACK_SIZE '
	test_have_prereq !MINGW,!CYGWIN &&
	run_with_limited_stack true
'

build_option () {
	git version --build-options |
	sed -ne "s/^$1: //p"
}

test_lazy_prereq LONG_IS_64BIT '
	test 8 -le "$(build_option sizeof-long)"
'

test_lazy_prereq TIME_IS_64BIT 'test-tool date is64bit'
test_lazy_prereq TIME_T_IS_64BIT 'test-tool date time_t-is64bit'

test_lazy_prereq CURL '
	curl --version
'

# SHA1 is a test if the hash algorithm in use is SHA-1.  This is both for tests
# which will not work with other hash algorithms and tests that work but don't
# test anything meaningful (e.g. special values which cause short collisions).
test_lazy_prereq SHA1 '
	test $(git hash-object /dev/null) = e69de29bb2d1d6434b8b29ae775ad8c2e48c5391
'

test_lazy_prereq REBASE_P '
	test -z "$GIT_TEST_SKIP_REBASE_P"
'
