#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test fsck --lost-found'

. ./test-lib.sh

test_expect_success setup '
	git config core.logAllRefUpdates 0 &&
	: > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m initial &&
	echo 1 > file1 &&
	echo 2 > file2 &&
	git add file1 file2 &&
	test_tick &&
	git commit -m second &&
	echo 3 > file3 &&
	git add file3
'

test_expect_success 'lost and found something' '
	git rev-parse HEAD > lost-commit &&
	git rev-parse :file3 > lost-other &&
	test_tick &&
	git reset --hard HEAD^ &&
	git fsck --lost-found &&
	ls .git/lost-found/*/* >actual &&
	cat >expect <<-EOF &&
	.git/lost-found/commit/$(cat lost-commit)
	.git/lost-found/other/$(cat lost-other)
	EOF
	test_cmp expect actual
'

test_done
