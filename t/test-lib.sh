#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

# if --tee was passed, write the output not only to the terminal, but
# additionally to the file test-results/$BASENAME.out, too.
case "$GIT_TEST_TEE_STARTED, $* " in
done,*)
	# do not redirect again
	;;
*' --tee '*|*' --va'*)
	mkdir -p test-results
	BASE=test-results/$(basename "$0" .sh)
	(GIT_TEST_TEE_STARTED=done ${SHELL-sh} "$0" "$@" 2>&1;
	 echo $? > $BASE.exit) | tee $BASE.out
	test "$(cat $BASE.exit)" = 0
	exit
	;;
esac

# Keep the original TERM for say_color
ORIGINAL_TERM=$TERM

# For repeatability, reset the environment to known value.
LANG=C
LC_ALL=C
PAGER=cat
TZ=UTC
TERM=dumb
export LANG LC_ALL PAGER TERM TZ
EDITOR=:
VISUAL=:
unset GIT_EDITOR
unset AUTHOR_DATE
unset AUTHOR_EMAIL
unset AUTHOR_NAME
unset COMMIT_AUTHOR_EMAIL
unset COMMIT_AUTHOR_NAME
unset EMAIL
unset GIT_ALTERNATE_OBJECT_DIRECTORIES
unset GIT_AUTHOR_DATE
GIT_AUTHOR_EMAIL=author@example.com
GIT_AUTHOR_NAME='A U Thor'
unset GIT_COMMITTER_DATE
GIT_COMMITTER_EMAIL=committer@example.com
GIT_COMMITTER_NAME='C O Mitter'
unset GIT_DIFF_OPTS
unset GIT_DIR
unset GIT_WORK_TREE
unset GIT_EXTERNAL_DIFF
unset GIT_INDEX_FILE
unset GIT_OBJECT_DIRECTORY
unset GIT_CEILING_DIRECTORIES
unset SHA1_FILE_DIRECTORIES
unset SHA1_FILE_DIRECTORY
GIT_MERGE_VERBOSITY=5
export GIT_MERGE_VERBOSITY
export GIT_AUTHOR_EMAIL GIT_AUTHOR_NAME
export GIT_COMMITTER_EMAIL GIT_COMMITTER_NAME
export EDITOR VISUAL
GIT_TEST_CMP=${GIT_TEST_CMP:-diff -u}

# Protect ourselves from common misconfiguration to export
# CDPATH into the environment
unset CDPATH

case $(echo $GIT_TRACE |tr "[A-Z]" "[a-z]") in
	1|2|true)
		echo "* warning: Some tests will not work if GIT_TRACE" \
			"is set as to trace on STDERR ! *"
		echo "* warning: Please set GIT_TRACE to something" \
			"other than 1, 2 or true ! *"
		;;
esac

# Each test should start with something like this, after copyright notices:
#
# test_description='Description of this test...
# This test checks if command xyzzy does the right thing...
# '
# . ./test-lib.sh
[ "x$ORIGINAL_TERM" != "xdumb" ] && (
		TERM=$ORIGINAL_TERM &&
		export TERM &&
		[ -t 1 ] &&
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
	-h|--h|--he|--hel|--help)
		help=t; shift ;;
	-v|--v|--ve|--ver|--verb|--verbo|--verbos|--verbose)
		verbose=t; shift ;;
	-q|--q|--qu|--qui|--quie|--quiet)
		quiet=t; shift ;;
	--no-color)
		color=; shift ;;
	--no-python)
		# noop now...
		shift ;;
	--va|--val|--valg|--valgr|--valgri|--valgrin|--valgrind)
		valgrind=t; verbose=t; shift ;;
	--tee)
		shift ;; # was handled already
	*)
		break ;;
	esac
done

if test -n "$color"; then
	say_color () {
		(
		TERM=$ORIGINAL_TERM
		export TERM
		case "$1" in
			error) tput bold; tput setaf 1;; # bold red
			skip)  tput bold; tput setaf 2;; # bold green
			pass)  tput setaf 2;;            # green
			info)  tput setaf 3;;            # brown
			*) test -n "$quiet" && return;;
		esac
		shift
		printf "* %s" "$*"
		tput sgr0
		echo
		)
	}
else
	say_color() {
		test -z "$1" && test -n "$quiet" && return
		shift
		echo "* $*"
	}
fi

error () {
	say_color error "error: $*"
	trap - EXIT
	exit 1
}

say () {
	say_color info "$*"
}

test "${test_description}" != "" ||
error "Test script did not set test_description."

if test "$help" = "t"
then
	echo "$test_description"
	exit 0
fi

exec 5>&1
if test "$verbose" = "t"
then
	exec 4>&2 3>&1
else
	exec 4>/dev/null 3>/dev/null
fi

test_failure=0
test_count=0
test_fixed=0
test_broken=0
test_success=0

die () {
	echo >&5 "FATAL: Unexpected exit with code $?"
	exit 1
}

trap 'die' EXIT

# The semantics of the editor variables are that of invoking
# sh -c "$EDITOR \"$@\"" files ...
#
# If our trash directory contains shell metacharacters, they will be
# interpreted if we just set $EDITOR directly, so do a little dance with
# environment variables to work around this.
#
# In particular, quoting isn't enough, as the path may contain the same quote
# that we're using.
test_set_editor () {
	FAKE_EDITOR="$1"
	export FAKE_EDITOR
	VISUAL='"$FAKE_EDITOR"'
	export VISUAL
}

test_tick () {
	if test -z "${test_tick+set}"
	then
		test_tick=1112911993
	else
		test_tick=$(($test_tick + 60))
	fi
	GIT_COMMITTER_DATE="$test_tick -0700"
	GIT_AUTHOR_DATE="$test_tick -0700"
	export GIT_COMMITTER_DATE GIT_AUTHOR_DATE
}

# Call test_commit with the arguments "<message> [<file> [<contents>]]"
#
# This will commit a file with the given contents and the given commit
# message.  It will also add a tag with <message> as name.
#
# Both <file> and <contents> default to <message>.

test_commit () {
	file=${2:-"$1.t"}
	echo "${3-$1}" > "$file" &&
	git add "$file" &&
	test_tick &&
	git commit -m "$1" &&
	git tag "$1"
}

# Call test_merge with the arguments "<message> <commit>", where <commit>
# can be a tag pointing to the commit-to-merge.

test_merge () {
	test_tick &&
	git merge -m "$1" "$2" &&
	git tag "$1"
}

# This function helps systems where core.filemode=false is set.
# Use it instead of plain 'chmod +x' to set or unset the executable bit
# of a file in the working directory and add it to the index.

test_chmod () {
	chmod "$@" &&
	git update-index --add "--chmod=$@"
}

# Use test_set_prereq to tell that a particular prerequisite is available.
# The prerequisite can later be checked for in two ways:
#
# - Explicitly using test_have_prereq.
#
# - Implicitly by specifying the prerequisite tag in the calls to
#   test_expect_{success,failure,code}.
#
# The single parameter is the prerequisite tag (a simple word, in all
# capital letters by convention).

test_set_prereq () {
	satisfied="$satisfied$1 "
}
satisfied=" "

test_have_prereq () {
	case $satisfied in
	*" $1 "*)
		: yes, have it ;;
	*)
		! : nope ;;
	esac
}

# You are not expected to call test_ok_ and test_failure_ directly, use
# the text_expect_* functions instead.

test_ok_ () {
	test_success=$(($test_success + 1))
	say_color "" "  ok $test_count: $@"
}

test_failure_ () {
	test_failure=$(($test_failure + 1))
	say_color error "FAIL $test_count: $1"
	shift
	echo "$@" | sed -e 's/^/	/'
	test "$immediate" = "" || { trap - EXIT; exit 1; }
}

test_known_broken_ok_ () {
	test_fixed=$(($test_fixed+1))
	say_color "" "  FIXED $test_count: $@"
}

test_known_broken_failure_ () {
	test_broken=$(($test_broken+1))
	say_color skip "  still broken $test_count: $@"
}

test_debug () {
	test "$debug" = "" || eval "$1"
}

test_run_ () {
	eval >&3 2>&4 "$1"
	eval_ret="$?"
	return 0
}

test_skip () {
	test_count=$(($test_count+1))
	to_skip=
	for skp in $GIT_SKIP_TESTS
	do
		case $this_test.$test_count in
		$skp)
			to_skip=t
		esac
	done
	if test -z "$to_skip" && test -n "$prereq" &&
	   ! test_have_prereq "$prereq"
	then
		to_skip=t
	fi
	case "$to_skip" in
	t)
		say_color skip >&3 "skipping test: $@"
		say_color skip "skip $test_count: $1"
		: true
		;;
	*)
		false
		;;
	esac
}

test_expect_failure () {
	test "$#" = 3 && { prereq=$1; shift; } || prereq=
	test "$#" = 2 ||
	error "bug in the test script: not 2 or 3 parameters to test-expect-failure"
	if ! test_skip "$@"
	then
		say >&3 "checking known breakage: $2"
		test_run_ "$2"
		if [ "$?" = 0 -a "$eval_ret" = 0 ]
		then
			test_known_broken_ok_ "$1"
		else
		    test_known_broken_failure_ "$1"
		fi
	fi
	echo >&3 ""
}

test_expect_success () {
	test "$#" = 3 && { prereq=$1; shift; } || prereq=
	test "$#" = 2 ||
	error "bug in the test script: not 2 or 3 parameters to test-expect-success"
	if ! test_skip "$@"
	then
		say >&3 "expecting success: $2"
		test_run_ "$2"
		if [ "$?" = 0 -a "$eval_ret" = 0 ]
		then
			test_ok_ "$1"
		else
			test_failure_ "$@"
		fi
	fi
	echo >&3 ""
}

test_expect_code () {
	test "$#" = 4 && { prereq=$1; shift; } || prereq=
	test "$#" = 3 ||
	error "bug in the test script: not 3 or 4 parameters to test-expect-code"
	if ! test_skip "$@"
	then
		say >&3 "expecting exit code $1: $3"
		test_run_ "$3"
		if [ "$?" = 0 -a "$eval_ret" = "$1" ]
		then
			test_ok_ "$2"
		else
			test_failure_ "$@"
		fi
	fi
	echo >&3 ""
}

# test_external runs external test scripts that provide continuous
# test output about their progress, and succeeds/fails on
# zero/non-zero exit code.  It outputs the test output on stdout even
# in non-verbose mode, and announces the external script with "* run
# <n>: ..." before running it.  When providing relative paths, keep in
# mind that all scripts run in "trash directory".
# Usage: test_external description command arguments...
# Example: test_external 'Perl API' perl ../path/to/test.pl
test_external () {
	test "$#" = 4 && { prereq=$1; shift; } || prereq=
	test "$#" = 3 ||
	error >&5 "bug in the test script: not 3 or 4 parameters to test_external"
	descr="$1"
	shift
	if ! test_skip "$descr" "$@"
	then
		# Announce the script to reduce confusion about the
		# test output that follows.
		say_color "" " run $test_count: $descr ($*)"
		# Run command; redirect its stderr to &4 as in
		# test_run_, but keep its stdout on our stdout even in
		# non-verbose mode.
		"$@" 2>&4
		if [ "$?" = 0 ]
		then
			test_ok_ "$descr"
		else
			test_failure_ "$descr" "$@"
		fi
	fi
}

# Like test_external, but in addition tests that the command generated
# no output on stderr.
test_external_without_stderr () {
	# The temporary file has no (and must have no) security
	# implications.
	tmp="$TMPDIR"; if [ -z "$tmp" ]; then tmp=/tmp; fi
	stderr="$tmp/git-external-stderr.$$.tmp"
	test_external "$@" 4> "$stderr"
	[ -f "$stderr" ] || error "Internal error: $stderr disappeared."
	descr="no stderr: $1"
	shift
	say >&3 "expecting no stderr from previous command"
	if [ ! -s "$stderr" ]; then
		rm "$stderr"
		test_ok_ "$descr"
	else
		if [ "$verbose" = t ]; then
			output=`echo; echo Stderr is:; cat "$stderr"`
		else
			output=
		fi
		# rm first in case test_failure exits.
		rm "$stderr"
		test_failure_ "$descr" "$@" "$output"
	fi
}

# This is not among top-level (test_expect_success | test_expect_failure)
# but is a prefix that can be used in the test script, like:
#
#	test_expect_success 'complain and die' '
#           do something &&
#           do something else &&
#	    test_must_fail git checkout ../outerspace
#	'
#
# Writing this as "! git checkout ../outerspace" is wrong, because
# the failure could be due to a segv.  We want a controlled failure.

test_must_fail () {
	"$@"
	test $? -gt 0 -a $? -le 129 -o $? -gt 192
}

# test_cmp is a helper function to compare actual and expected output.
# You can use it like:
#
#	test_expect_success 'foo works' '
#		echo expected >expected &&
#		foo >actual &&
#		test_cmp expected actual
#	'
#
# This could be written as either "cmp" or "diff -u", but:
# - cmp's output is not nearly as easy to read as diff -u
# - not all diff versions understand "-u"

test_cmp() {
	$GIT_TEST_CMP "$@"
}

# Most tests can use the created repository, but some may need to create more.
# Usage: test_create_repo <directory>
test_create_repo () {
	test "$#" = 1 ||
	error "bug in the test script: not 1 parameter to test-create-repo"
	owd=`pwd`
	repo="$1"
	mkdir -p "$repo"
	cd "$repo" || error "Cannot setup test environment"
	"$GIT_EXEC_PATH/git-init" "--template=$TEST_DIRECTORY/../templates/blt/" >&3 2>&4 ||
	error "cannot run git init -- have you built things yet?"
	mv .git/hooks .git/hooks-disabled
	cd "$owd"
}

test_done () {
	trap - EXIT
	test_results_dir="$TEST_DIRECTORY/test-results"
	mkdir -p "$test_results_dir"
	test_results_path="$test_results_dir/${0%.sh}-$$"

	echo "total $test_count" >> $test_results_path
	echo "success $test_success" >> $test_results_path
	echo "fixed $test_fixed" >> $test_results_path
	echo "broken $test_broken" >> $test_results_path
	echo "failed $test_failure" >> $test_results_path
	echo "" >> $test_results_path

	if test "$test_fixed" != 0
	then
		say_color pass "fixed $test_fixed known breakage(s)"
	fi
	if test "$test_broken" != 0
	then
		say_color error "still have $test_broken known breakage(s)"
		msg="remaining $(($test_count-$test_broken)) test(s)"
	else
		msg="$test_count test(s)"
	fi
	case "$test_failure" in
	0)
		say_color pass "passed all $msg"

		test -d "$remove_trash" &&
		cd "$(dirname "$remove_trash")" &&
		rm -rf "$(basename "$remove_trash")"

		exit 0 ;;

	*)
		say_color error "failed $test_failure among $msg"
		exit 1 ;;

	esac
}

# Test the binaries we have just built.  The tests are kept in
# t/ subdirectory and are run in 'trash directory' subdirectory.
TEST_DIRECTORY=$(pwd)
if test -z "$valgrind"
then
	if test -z "$GIT_TEST_INSTALLED"
	then
		PATH=$TEST_DIRECTORY/..:$PATH
		GIT_EXEC_PATH=$TEST_DIRECTORY/..
	else
		GIT_EXEC_PATH=$($GIT_TEST_INSTALLED/git --exec-path)  ||
		error "Cannot run git from $GIT_TEST_INSTALLED."
		PATH=$GIT_TEST_INSTALLED:$TEST_DIRECTORY/..:$PATH
		GIT_EXEC_PATH=${GIT_TEST_EXEC_PATH:-$GIT_EXEC_PATH}
	fi
else
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
		# handle only executables
		test -x "$1" || return

		base=$(basename "$1")
		symlink_target=$TEST_DIRECTORY/../$base
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
	for file in $TEST_DIRECTORY/../git* $TEST_DIRECTORY/../test-*
	do
		make_valgrind_symlink $file
	done
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
fi
GIT_TEMPLATE_DIR=$(pwd)/../templates/blt
unset GIT_CONFIG
GIT_CONFIG_NOSYSTEM=1
GIT_CONFIG_NOGLOBAL=1
export PATH GIT_EXEC_PATH GIT_TEMPLATE_DIR GIT_CONFIG_NOSYSTEM GIT_CONFIG_NOGLOBAL

GITPERLLIB=$(pwd)/../perl/blib/lib:$(pwd)/../perl/blib/arch/auto/Git
export GITPERLLIB
test -d ../templates/blt || {
	error "You haven't built things yet, have you?"
}

if ! test -x ../test-chmtime; then
	echo >&2 'You need to build test-chmtime:'
	echo >&2 'Run "make test-chmtime" in the source (toplevel) directory'
	exit 1
fi

. ../GIT-BUILD-OPTIONS

# Test repository
test="trash directory.$(basename "$0" .sh)"
test ! -z "$debug" || remove_trash="$TEST_DIRECTORY/$test"
rm -fr "$test" || {
	trap - EXIT
	echo >&5 "FATAL: Cannot prepare test area"
	exit 1
}

test_create_repo "$test"
# Use -P to resolve symlinks in our working directory so that the cwd
# in subprocesses like git equals our $PWD (for pathname comparisons).
cd -P "$test" || exit 1

this_test=${0##*/}
this_test=${this_test%%-*}
for skp in $GIT_SKIP_TESTS
do
	to_skip=
	for skp in $GIT_SKIP_TESTS
	do
		case "$this_test" in
		$skp)
			to_skip=t
		esac
	done
	case "$to_skip" in
	t)
		say_color skip >&3 "skipping test $this_test altogether"
		say_color skip "skip all tests in $this_test"
		test_done
	esac
done

# Fix some commands on Windows
case $(uname -s) in
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
	;;
*)
	test_set_prereq POSIXPERM
	test_set_prereq BSLASHPSPEC
	test_set_prereq EXECKEEPSPID
	;;
esac

test -z "$NO_PERL" && test_set_prereq PERL

# test whether the filesystem supports symbolic links
ln -s x y 2>/dev/null && test -h y 2>/dev/null && test_set_prereq SYMLINKS
rm -f y
