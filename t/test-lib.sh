#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

# For repeatability, reset the environment to known value.
LANG=C
LC_ALL=C
PAGER=cat
TZ=UTC
export LANG LC_ALL PAGER TZ
EDITOR=:
VISUAL=:
unset AUTHOR_DATE
unset AUTHOR_EMAIL
unset AUTHOR_NAME
unset COMMIT_AUTHOR_EMAIL
unset COMMIT_AUTHOR_NAME
unset GIT_ALTERNATE_OBJECT_DIRECTORIES
unset GIT_AUTHOR_DATE
GIT_AUTHOR_EMAIL=author@example.com
GIT_AUTHOR_NAME='A U Thor'
unset GIT_COMMITTER_DATE
GIT_COMMITTER_EMAIL=committer@example.com
GIT_COMMITTER_NAME='C O Mitter'
unset GIT_DIFF_OPTS
unset GIT_DIR
unset GIT_EXTERNAL_DIFF
unset GIT_INDEX_FILE
unset GIT_OBJECT_DIRECTORY
unset SHA1_FILE_DIRECTORIES
unset SHA1_FILE_DIRECTORY
export GIT_AUTHOR_EMAIL GIT_AUTHOR_NAME
export GIT_COMMITTER_EMAIL GIT_COMMITTER_NAME
export EDITOR VISUAL

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

error () {
	echo "* error: $*"
	trap - exit
	exit 1
}

say () {
	echo "* $*"
}

test "${test_description}" != "" ||
error "Test script did not set test_description."

while test "$#" -ne 0
do
	case "$1" in
	-d|--d|--de|--deb|--debu|--debug)
		debug=t; shift ;;
	-i|--i|--im|--imm|--imme|--immed|--immedi|--immedia|--immediat|--immediate)
		immediate=t; shift ;;
	-h|--h|--he|--hel|--help)
		echo "$test_description"
		exit 0 ;;
	-v|--v|--ve|--ver|--verb|--verbo|--verbos|--verbose)
		verbose=t; shift ;;
	--no-python)
		# noop now...
		shift ;;
	*)
		break ;;
	esac
done

exec 5>&1
if test "$verbose" = "t"
then
	exec 4>&2 3>&1
else
	exec 4>/dev/null 3>/dev/null
fi

test_failure=0
test_count=0

trap 'echo >&5 "FATAL: Unexpected exit with code $?"; exit 1' exit


# You are not expected to call test_ok_ and test_failure_ directly, use
# the text_expect_* functions instead.

test_ok_ () {
	test_count=$(expr "$test_count" + 1)
	say "  ok $test_count: $@"
}

test_failure_ () {
	test_count=$(expr "$test_count" + 1)
	test_failure=$(expr "$test_failure" + 1);
	say "FAIL $test_count: $1"
	shift
	echo "$@" | sed -e 's/^/	/'
	test "$immediate" = "" || { trap - exit; exit 1; }
}


test_debug () {
	test "$debug" = "" || eval "$1"
}

test_run_ () {
	eval >&3 2>&4 "$1"
	eval_ret="$?"
	return 0
}

test_expect_failure () {
	test "$#" = 2 ||
	error "bug in the test script: not 2 parameters to test-expect-failure"
	say >&3 "expecting failure: $2"
	test_run_ "$2"
	if [ "$?" = 0 -a "$eval_ret" != 0 -a "$eval_ret" -lt 129 ]
	then
		test_ok_ "$1"
	else
		test_failure_ "$@"
	fi
	echo >&3 ""
}

test_expect_success () {
	test "$#" = 2 ||
	error "bug in the test script: not 2 parameters to test-expect-success"
	say >&3 "expecting success: $2"
	test_run_ "$2"
	if [ "$?" = 0 -a "$eval_ret" = 0 ]
	then
		test_ok_ "$1"
	else
		test_failure_ "$@"
	fi
	echo >&3 ""
}

test_expect_code () {
	test "$#" = 3 ||
	error "bug in the test script: not 3 parameters to test-expect-code"
	say >&3 "expecting exit code $1: $3"
	test_run_ "$3"
	if [ "$?" = 0 -a "$eval_ret" = "$1" ]
	then
		test_ok_ "$2"
	else
		test_failure_ "$@"
	fi
	echo >&3 ""
}

# Most tests can use the created repository, but some amy need to create more.
# Usage: test_create_repo <directory>
test_create_repo () {
	test "$#" = 1 ||
	error "bug in the test script: not 1 parameter to test-create-repo"
	owd=`pwd`
	repo="$1"
	mkdir "$repo"
	cd "$repo" || error "Cannot setup test environment"
	"$GIT_EXEC_PATH/git" init-db --template=$GIT_EXEC_PATH/templates/blt/ 2>/dev/null ||
	error "cannot run git init-db -- have you built things yet?"
	mv .git/hooks .git/hooks-disabled
	cd "$owd"
}
	
# Many tests do init-db and clone but they must be told about the freshly
# built templates.
git_init_db () {
	git init-db --template="$GIT_EXEC_PATH/templates/blt/" "$@"
}

git_clone () {
	git clone --template="$GIT_EXEC_PATH/templates/blt/" "$@"
}

test_done () {
	trap - exit
	case "$test_failure" in
	0)
		# We could:
		# cd .. && rm -fr trash
		# but that means we forbid any tests that use their own
		# subdirectory from calling test_done without coming back
		# to where they started from.
		# The Makefile provided will clean this test area so
		# we will leave things as they are.

		say "passed all $test_count test(s)"
		exit 0 ;;

	*)
		say "failed $test_failure among $test_count test(s)"
		exit 1 ;;

	esac
}

# Test the binaries we have just built.  The tests are kept in
# t/ subdirectory and are run in trash subdirectory.
PATH=$(pwd)/..:$PATH
GIT_EXEC_PATH=$(pwd)/..
HOME=$(pwd)/trash
export PATH GIT_EXEC_PATH HOME

GITPERLLIB=$(pwd)/../perl/blib/lib:$(pwd)/../perl/blib/arch/auto/Git
export GITPERLLIB
test -d ../templates/blt || {
	error "You haven't built things yet, have you?"
}

# Test repository
test=trash
rm -fr "$test"
test_create_repo $test
cd "$test"
