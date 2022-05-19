#!/bin/sh
#
# Copyright (c) 2009 Stephen Boyd
#

test_description='but apply --build-fake-ancestor handling.'

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit 1 &&
	test_cummit 2 &&
	mkdir sub &&
	test_cummit 3 sub/3.t &&
	test_cummit 4
'

test_expect_success 'apply --build-fake-ancestor' '
	but checkout 2 &&
	echo "A" > 1.t &&
	but diff > 1.patch &&
	but reset --hard &&
	but checkout 1 &&
	but apply --build-fake-ancestor 1.ancestor 1.patch
'

test_expect_success 'apply --build-fake-ancestor in a subdirectory' '
	but checkout 3 &&
	echo "C" > sub/3.t &&
	but diff > 3.patch &&
	but reset --hard &&
	but checkout 4 &&
	(
		cd sub &&
		but apply --build-fake-ancestor 3.ancestor ../3.patch &&
		test -f 3.ancestor
	) &&
	but apply --build-fake-ancestor 3.ancestor 3.patch &&
	test_cmp sub/3.ancestor 3.ancestor
'

test_done
