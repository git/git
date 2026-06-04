#!/bin/sh

test_description='refs exists'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

git_show_ref_exists='git refs exists'
. "$TEST_DIRECTORY"/show-ref-exists-tests.sh
