#!/bin/sh

test_description='test read-tree into a fresh index file'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	echo one >a &&
	but add a &&
	but cummit -m initial
'

test_expect_success 'non-existent index file' '
	rm -f new-index &&
	GIT_INDEX_FILE=new-index but read-tree main
'

test_expect_success 'empty index file' '
	rm -f new-index &&
	> new-index &&
	GIT_INDEX_FILE=new-index but read-tree main
'

test_done

