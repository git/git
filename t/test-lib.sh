#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

# For repeatability, reset the environment to known value.
LANG=C
TZ=UTC
export LANG TZ
unset AUTHOR_DATE
unset AUTHOR_EMAIL
unset AUTHOR_NAME
unset COMMIT_AUTHOR_EMAIL
unset COMMIT_AUTHOR_NAME
unset GIT_ALTERNATE_OBJECT_DIRECTORIES
unset GIT_AUTHOR_DATE
unset GIT_AUTHOR_EMAIL
unset GIT_AUTHOR_NAME
unset GIT_COMMITTER_EMAIL
unset GIT_COMMITTER_NAME
unset GIT_DIFF_OPTS
unset GIT_DIR
unset GIT_EXTERNAL_DIFF
unset GIT_INDEX_FILE
unset GIT_OBJECT_DIRECTORY
unset SHA1_FILE_DIRECTORIES
unset SHA1_FILE_DIRECTORY

# Each test should start with something like this, after copyright notices:
#
# test_description='Description of this test...
# This test checks if command xyzzy does the right thing...
# '
# . ./test-lib.sh

error () {
	echo "* error: $*"
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
	*)
		break ;;
	esac
done

if test "$verbose" = "t"
then
	exec 4>&2 3>&1
else
	exec 4>/dev/null 3>/dev/null
fi

test_failure=0
test_count=0


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
	test "$immediate" = "" || exit 1
}


test_debug () {
	test "$debug" = "" || eval "$1"
}

test_expect_failure () {
	test "$#" = 2 ||
	error "bug in the test script: not 2 parameters to test-expect-failure"
	say >&3 "expecting failure: $2"
	if eval >&3 2>&4 "$2"
	then
		test_failure_ "$@"
	else
		test_ok_ "$1"
	fi
}

test_expect_success () {
	test "$#" = 2 ||
	error "bug in the test script: not 2 parameters to test-expect-success"
	say >&3 "expecting success: $2"
	if eval >&3 2>&4 "$2"
	then
		test_ok_ "$1"
	else
		test_failure_ "$@"
	fi
}

test_done () {
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

# Test repository
test=trash
rm -fr "$test"
mkdir "$test"
cd "$test"
git-init-db 2>/dev/null || error "cannot run git-init-db"
