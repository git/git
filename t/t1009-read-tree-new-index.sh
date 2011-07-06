#!/bin/sh

test_description='test read-tree into a fresh index file'

. ./test-lib.sh

test_expect_success setup '
	echo one >a &&
	git add a &&
	git commit -m initial
'

test_expect_success 'non-existent index file' '
	rm -f new-index &&
	GIT_INDEX_FILE=new-index git read-tree master
'

test_expect_success 'empty index file' '
	rm -f new-index &&
	> new-index &&
	GIT_INDEX_FILE=new-index git read-tree master
'

test_done

