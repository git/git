#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test fsck --lost-found'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	but config core.logAllRefUpdates 0 &&
	: > file1 &&
	but add file1 &&
	test_tick &&
	but cummit -m initial &&
	echo 1 > file1 &&
	echo 2 > file2 &&
	but add file1 file2 &&
	test_tick &&
	but cummit -m second &&
	echo 3 > file3 &&
	but add file3
'

test_expect_success 'lost and found something' '
	but rev-parse HEAD > lost-cummit &&
	but rev-parse :file3 > lost-other &&
	test_tick &&
	but reset --hard HEAD^ &&
	but fsck --lost-found &&
	test 2 = $(ls .but/lost-found/*/* | wc -l) &&
	test -f .but/lost-found/cummit/$(cat lost-cummit) &&
	test -f .but/lost-found/other/$(cat lost-other)
'

test_done
