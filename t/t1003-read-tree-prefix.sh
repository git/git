#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='but read-tree --prefix test.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	echo hello >one &&
	but update-index --add one &&
	tree=$(but write-tree) &&
	echo tree is $tree
'

echo 'one
two/one' >expect

test_expect_success 'read-tree --prefix' '
	but read-tree --prefix=two/ $tree &&
	but ls-files >actual &&
	cmp expect actual
'

test_expect_success 'read-tree --prefix with leading slash exits with error' '
	but rm -rf . &&
	test_must_fail but read-tree --prefix=/two/ $tree &&
	but read-tree --prefix=two/ $tree &&

	but rm -rf . &&
	test_must_fail but read-tree --prefix=/ $tree &&
	but read-tree --prefix= $tree
'

test_done
