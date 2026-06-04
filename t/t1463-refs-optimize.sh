#!/bin/sh

test_description='git refs optimize should not change the branch semantic

This test runs git refs optimize and git show-ref and checks that the branch
semantic is still the same.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
GIT_TEST_DEFAULT_REF_FORMAT=files
export GIT_TEST_DEFAULT_REF_FORMAT

. ./test-lib.sh

pack_refs='refs optimize'
. "$TEST_DIRECTORY"/pack-refs-tests.sh

test_done
