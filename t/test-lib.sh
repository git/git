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

if test ! -f "$GIT_BUILD_DIR"/GIT-BUILD-OPTIONS
then
	echo >&2 'error: GIT-BUILD-OPTIONS missing (has Git been built?).'
	exit 1
fi

. "$GIT_BUILD_DIR"/GIT-BUILD-OPTIONS
export PERL_PATH SHELL_PATH

test -z "$MSVC_DEPS" ||
PATH="$GIT_BUILD_DIR/$MSVC_DEPS/bin:$PATH"

################################################################
# It appears that people try to run tests without building...
"$GIT_BUILD_DIR/git$X" >/dev/null
if test $? != 1
then
	echo >&2 'error: you do not seem to have built git yet.'
	exit 1
fi

# if --tee was passed, write the output not only to the terminal, but
# additionally to the file test-results/$BASENAME.out, too.
case "$GIT_TEST_TEE_STARTED, $* " in
done,*)
	# do not redirect again
	;;
*' --tee '*|*' --va'*|*' --verbose-log '*)
	mkdir -p "$TEST_OUTPUT_DIRECTORY/test-results"
	BASE="$TEST_OUTPUT_DIRECTORY/test-results/$(basename "$0" .sh)"

	# Make this filename available to the sub-process in case it is using
	# --verbose-log.
	GIT_TEST_TEE_OUTPUT_FILE=$BASE.out
	export GIT_TEST_TEE_OUTPUT_FILE

	# Truncate before calling "tee -a" to get rid of the results
	# from any previous runs.
	>"$GIT_TEST_TEE_OUTPUT_FILE"

	(GIT_TEST_TEE_STARTED=done ${SHELL_PATH} "$0" "$@" 2>&1;
	 echo $? >"$BASE.exit") | tee -a "$GIT_TEST_TEE_OUTPUT_FILE"
	test "$(cat "$BASE.exit")" = 0
	exit
	;;
esac

# For repeatability, reset the environment to known value.
# TERM is sanitized below, after saving color control sequences.
LANG=C
LC_ALL=C
PAGER=cat
TZ=UTC
export LANG LC_ALL PAGER TZ
EDITOR=:
# A call to "unset" with no arguments causes at least Solaris 10
# /usr/xpg4/bin/sh and /bin/ksh to bail out.  So keep the unsets
# deriving from the command substitution clustered with the other
# ones.
unset VISUAL EMAIL LANGUAGE COLUMNS $(env | sed -n \
	-e '/^GIT_TRACE/d' \
	-e '/^GIT_DEBUG/d' \
	-e '/^GIT_USE_LOOKUP/d' \
	-e '/^GIT_TEST/d' \
	-e '/^GIT_.*_TEST/d' \
	-e '/^GIT_PROVE/d' \
	-e '/^GIT_VALGRIND/d' \
	-e '/^GIT_UNZIP/d' \
	-e '/^GIT_PERF_/d' \
	-e '/^GIT_CURL_VERBOSE/d' \
	-e '/^GIT_TRACE_CURL/d' \
	-e 's/^\(GIT_[^=]*\)=.*/\1/p')
unset XDG_CONFIG_HOME
unset GITPERLLIB
GIT_AUTHOR_EMAIL=author@example.com
GIT_AUTHOR_NAME='A U Thor'
GIT_COMMITTER_EMAIL=committer@example.com
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

if test -n "${TEST_GIT_INDEX_VERSION:+isset}"
then
	GIT_INDEX_VERSION="$TEST_GIT_INDEX_VERSION"
	export GIT_INDEX_VERSION
fi

# Add libc MALLOC and MALLOC_PERTURB test
# only if we are not executing the test with valgrind
if expr " $GIT_TEST_OPTS " : ".* --valgrind " >/dev/null ||
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

# Convenience
#
# A regexp to match 5 and 40 hexdigits
_x05='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x05$_x05$_x05$_x05$_x05$_x05$_x05$_x05"

# Zero SHA-1
_z40=0000000000000000000000000000000000000000

EMPTY_TREE=4b825dc642cb6eb9a060e54bf8d69288fbee4904
EMPTY_BLOB=e69de29bb2d1d6434b8b29ae775ad8c2e48c5391

# Line feed
LF='
'

# UTF-8 ZERO WIDTH NON-JOINER, which HFS+ ignores
# when case-folding filenames
u200c=$(printf '\342\200\214')

export _x05 _x40 _z40 LF u200c EMPTY_TREE EMPTY_BLOB

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

while test "$#" -ne 0
do
	case "$1" in
	-d|--d|--de|--deb|--debu|--debug)
		debug=t; shift ;;
	-i|--i|--im|--imm|--imme|--immed|--immedi|--immedia|--immediat|--immediate)
		immediate=t; shift ;;
	-l|--l|--lo|--lon|--long|--long-|--long-t|--long-te|--long-tes|--long-test|--long-tests)
		GIT_TEST_LONG=t; export GIT_TEST_LONG; shift ;;
	-r)
		shift; test "$#" -ne 0 || {
			echo 'error: -r requires an argument' >&2;
			exit 1;
		}
		run_list=$1; shift ;;
	--run=*)
		run_list=${1#--*=}; shift ;;
	-h|--h|--he|--hel|--help)
		help=t; shift ;;
	-v|--v|--ve|--ver|--verb|--verbo|--verbos|--verbose)
		verbose=t; shift ;;
	--verbose-only=*)
		verbose_only=${1#--*=}
		shift ;;
	-q|--q|--qu|--qui|--quie|--quiet)
		# Ignore --quiet under a TAP::Harness. Saying how many tests
		# passed without the ok/not ok details is always an error.
		test -z "$HARNESS_ACTIVE" && quiet=t; shift ;;
	--with-dashes)
		with_dashes=t; shift ;;
	--no-color)
		color=; shift ;;
	--va|--val|--valg|--valgr|--valgri|--valgrin|--valgrind)
		valgrind=memcheck
		shift ;;
	--valgrind=*)
		valgrind=${1#--*=}
		shift ;;
	--valgrind-only=*)
		valgrind_only=${1#--*=}
		shift ;;
	--tee)
		shift ;; # was handled already
	--root=*)
		root=${1#--*=}
		shift ;;
	--chain-lint)
		GIT_TEST_CHAIN_LINT=1
		shift ;;
	--no-chain-lint)
		GIT_TEST_CHAIN_LINT=0
		shift ;;
	-x)
		trace=t
		verbose=t
		shift ;;
	--verbose-log)
		verbose_log=t
		shift ;;
	*)
		echo "error: unknown test option '$1'" >&2; exit 1 ;;
	esac
done

if test -n "$valgrind_only"
then
	test -z "$valgrind" && valgrind=memcheck
	test -z "$verbose" && verbose_only="$valgrind_only"
elif test -n "$valgrind"
then
	verbose=t
fi

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
	GIT_EXIT_OK=t
	exit 1
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
trap 'exit $?' INT

# The user-facing functions are loaded from a separate file so that
# test_perf subshells can have them too
. "$TEST_DIRECTORY/test-lib-functions.sh"

# You are not expected to call test_ok_ and test_failure_ directly, use
# the test_expect_* functions instead.

test_ok_ () {
	test_success=$(($test_success + 1))
	say_color "" "ok $test_count - $@"
}

test_failure_ () {
	test_failure=$(($test_failure + 1))
	say_color error "not ok $test_count - $1"
	shift
	printf '%s\n' "$*" | sed -e 's/^/#	/'
	test "$immediate" = "" || { GIT_EXIT_OK=t; exit 1; }
}

test_known_broken_ok_ () {
	test_fixed=$(($test_fixed+1))
	say_color error "ok $test_count - $@ # TODO known breakage vanished"
}

test_known_broken_failure_ () {
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
	test "$trace" = t && test "$verbose" = t
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
	# We run this block with stderr redirected to avoid extra cruft
	# during a "-x" trace. Once in "set -x" mode, we cannot prevent
	# the shell from printing the "set +x" to turn it off (nor the saving
	# of $? before that). But we can make sure that the output goes to
	# /dev/null.
	#
	# The test itself is run with stderr put back to &4 (so either to
	# /dev/null, or to the original stderr if --verbose was used).
	if test -n "$TEST_NO_REDIRECT"
	then
		test_eval_inner_ "$@"
		test_eval_ret_=$?
		if test "$trace" = t
		then
			set +x
			if test "$test_eval_ret_" != 0
			then
				say_color error >&4 "error: last command exited with \$?=$test_eval_ret_"
			fi
		fi
		return $test_eval_ret_
	fi
	{
		test_eval_inner_ "$@" </dev/null >&3 2>&4
		test_eval_ret_=$?
		if want_trace
		then
			set +x
			if test "$test_eval_ret_" != 0
			then
				say_color error >&4 "error: last command exited with \$?=$test_eval_ret_"
			fi
		fi
	} 2>/dev/null
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
		if test "OK-117" != "$(test_eval_ "(exit 117) && $1${LF}${LF}echo OK-\$?" 3>&1)"
		then
			error "bug in the test script: broken &&-chain or run-away HERE-DOC: $1"
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
}

test_finish_ () {
	echo >&3 ""
	maybe_teardown_valgrind
	maybe_teardown_verbose
}

test_skip () {
	to_skip=
	skipped_reason=
	if match_pattern_list $this_test.$test_count $GIT_SKIP_TESTS
	then
		to_skip=t
		skipped_reason="GIT_SKIP_TESTS"
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
	if test -z "$to_skip" && test -n "$run_list" &&
		! match_test_selector_list '--run' $test_count "$run_list"
	then
		to_skip=t
		skipped_reason="--run"
	fi

	case "$to_skip" in
	t)
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

test_done () {
	GIT_EXIT_OK=t

	if test -z "$HARNESS_ACTIVE"
	then
		test_results_dir="$TEST_OUTPUT_DIRECTORY/test-results"
		mkdir -p "$test_results_dir"
		base=${0##*/}
		test_results_path="$test_results_dir/${base%.sh}.counts"

		cat >"$test_results_path" <<-EOF
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
			rm -fr "$TRASH_DIRECTORY" ||
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
		test "# " = "$(head -c 2 <"$1")" ||
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
		    test "#!" != "$(head -c 2 < "$symlink_target")"
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
	GIT_EXEC_PATH=$GIT_BUILD_DIR
	if test -n "$with_dashes"
	then
		PATH="$GIT_BUILD_DIR:$PATH"
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

GITPERLLIB="$GIT_BUILD_DIR"/perl/blib/lib:"$GIT_BUILD_DIR"/perl/blib/arch/auto/Git
export GITPERLLIB
test -d "$GIT_BUILD_DIR"/templates/blt || {
	error "You haven't built things yet, have you?"
}

if ! test -x "$GIT_BUILD_DIR"/t/helper/test-chmtime
then
	echo >&2 'You need to build test-chmtime:'
	echo >&2 'Run "make t/helper/test-chmtime" in the source (toplevel) directory'
	exit 1
fi

# Test repository
TRASH_DIRECTORY="trash directory.$(basename "$0" .sh)"
test -n "$root" && TRASH_DIRECTORY="$root/$TRASH_DIRECTORY"
case "$TRASH_DIRECTORY" in
/*) ;; # absolute path is good
 *) TRASH_DIRECTORY="$TEST_OUTPUT_DIRECTORY/$TRASH_DIRECTORY" ;;
esac
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

# Provide an implementation of the 'yes' utility
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

# Fix some commands on Windows
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
	sum () {
		md5sum "$@"
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
test -z "$NO_GETTEXT" && test_set_prereq GETTEXT

# Can we rely on git's output in the C locale?
if test -n "$GETTEXT_POISON"
then
	GIT_GETTEXT_POISON=YesPlease
	export GIT_GETTEXT_POISON
	test_set_prereq GETTEXT_POISON
else
	test_set_prereq C_LOCALE_OUTPUT
fi

# Use this instead of test_cmp to compare files that contain expected and
# actual output from git commands that can be translated.  When running
# under GETTEXT_POISON this pretends that the command produced expected
# results.
test_i18ncmp () {
	test -n "$GETTEXT_POISON" || test_cmp "$@"
}

# Use this instead of "grep expected-string actual" to see if the
# output from a git command that can be translated either contains an
# expected string, or does not contain an unwanted one.  When running
# under GETTEXT_POISON this pretends that the command produced expected
# results.
test_i18ngrep () {
	if test -n "$GETTEXT_POISON"
	then
	    : # pretend success
	elif test "x!" = "x$1"
	then
		shift
		! grep "$@"
	else
		grep "$@"
	fi
}

test_lazy_prereq PIPE '
	# test whether the filesystem supports FIFOs
	case $(uname -s) in
	CYGWIN*|MINGW*)
		false
		;;
	*)
		rm -f testfifo && mkfifo testfifo
		;;
	esac
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

test_lazy_prereq UTF8_NFD_TO_NFC '
	# check whether FS converts nfd unicode to nfc
	auml=$(printf "\303\244")
	aumlcdiar=$(printf "\141\314\210")
	>"$auml" &&
	case "$(echo *)" in
	"$aumlcdiar")
		true ;;
	*)
		false ;;
	esac
'

test_lazy_prereq AUTOIDENT '
	sane_unset GIT_AUTHOR_NAME &&
	sane_unset GIT_AUTHOR_EMAIL &&
	git var GIT_AUTHOR_IDENT
'

test_lazy_prereq EXPENSIVE '
	test -n "$GIT_TEST_LONG"
'

test_lazy_prereq USR_BIN_TIME '
	test -x /usr/bin/time
'

test_lazy_prereq NOT_ROOT '
	uid=$(id -u) &&
	test "$uid" != 0
'

test_lazy_prereq JGIT '
	type jgit
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
	error "bug in test sript: cannot prepare SANETESTD"

	! test -r SANETESTD.1/x &&
	! rm SANETESTD.1/x && ! test -f SANETESTD.2/x
	status=$?

	chmod +rwx SANETESTD.1 SANETESTD.2 &&
	rm -rf SANETESTD.1 SANETESTD.2 ||
	error "bug in test sript: cannot clean SANETESTD"
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

test_lazy_prereq CMDLINE_LIMIT 'run_with_limited_cmdline true'

build_option () {
	git version --build-options |
	sed -ne "s/^$1: //p"
}

test_lazy_prereq LONG_IS_64BIT '
	test 8 -le "$(build_option sizeof-long)"
'

test_lazy_prereq TIME_IS_64BIT 'test-date is64bit'
test_lazy_prereq TIME_T_IS_64BIT 'test-date time_t-is64bit'
