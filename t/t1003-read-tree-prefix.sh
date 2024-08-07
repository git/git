#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git read-tree --prefix test.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	echo hello >one &&
	git update-index --add one &&
	tree=$(git write-tree) &&
	echo tree is $tree
'

echo 'one
two/one' >expect

test_expect_success 'read-tree --prefix' '
	git read-tree --prefix=two/ $tree &&
	git ls-files >actual &&
	cmp expect actual
'

test_expect_success 'read-tree --prefix with leading slash exits with error' '
	git rm -rf . &&
	test_must_fail git read-tree --prefix=/two/ $tree &&
	git read-tree --prefix=two/ $tree &&

	git rm -rf . &&
	test_must_fail git read-tree --prefix=/ $tree &&
	git read-tree --prefix= $tree
'

test_done
