#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test fsck --lost-found'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	git config core.logAllRefUpdates 0 &&
	: > file1 &&
	git add file1 &&
	test_tick &&
	git cummit -m initial &&
	echo 1 > file1 &&
	echo 2 > file2 &&
	git add file1 file2 &&
	test_tick &&
	git cummit -m second &&
	echo 3 > file3 &&
	git add file3
'

test_expect_success 'lost and found something' '
	git rev-parse HEAD > lost-cummit &&
	git rev-parse :file3 > lost-other &&
	test_tick &&
	git reset --hard HEAD^ &&
	git fsck --lost-found &&
	test 2 = $(ls .git/lost-found/*/* | wc -l) &&
	test -f .git/lost-found/cummit/$(cat lost-cummit) &&
	test -f .git/lost-found/other/$(cat lost-other)
'

test_done
