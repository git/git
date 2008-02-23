#!/bin/sh
#
# Copyright (c) 2008 Johannes E. Schindelin
#

test_description='prune'
. ./test-lib.sh

test_expect_success setup '

	: > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git gc

'

test_expect_success 'prune stale packs' '

	orig_pack=$(echo .git/objects/pack/*.pack) &&
	: > .git/objects/tmp_1.pack &&
	: > .git/objects/tmp_2.pack &&
	test-chmtime -86501 .git/objects/tmp_1.pack &&
	git prune --expire 1.day &&
	test -f $orig_pack &&
	test -f .git/objects/tmp_2.pack &&
	! test -f .git/objects/tmp_1.pack

'

test_done
