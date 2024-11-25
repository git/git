#!/bin/sh
#
# Copyright (c) 2009 Stephen Boyd
#

test_description='git apply --build-fake-ancestor handling.'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit 1 &&
	test_commit 2 &&
	mkdir sub &&
	test_commit 3 sub/3.t &&
	test_commit 4
'

test_expect_success 'apply --build-fake-ancestor' '
	git checkout 2 &&
	echo "A" > 1.t &&
	git diff > 1.patch &&
	git reset --hard &&
	git checkout 1 &&
	git apply --build-fake-ancestor 1.ancestor 1.patch
'

test_expect_success 'apply --build-fake-ancestor in a subdirectory' '
	git checkout 3 &&
	echo "C" > sub/3.t &&
	git diff > 3.patch &&
	git reset --hard &&
	git checkout 4 &&
	(
		cd sub &&
		git apply --build-fake-ancestor 3.ancestor ../3.patch &&
		test -f 3.ancestor
	) &&
	git apply --build-fake-ancestor 3.ancestor 3.patch &&
	test_cmp sub/3.ancestor 3.ancestor
'

test_done
