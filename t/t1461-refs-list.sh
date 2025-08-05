#!/bin/sh

test_description='git refs list tests'

. ./test-lib.sh

git_for_each_ref='git refs list'
. "$TEST_DIRECTORY"/for-each-ref-tests.sh
