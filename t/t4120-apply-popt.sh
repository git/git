#!/bin/sh
#
# Copyright (c) 2007 Shawn O. Pearce
#

test_description='git-apply -p handling.'

. ./test-lib.sh

test_expect_success setup '
	mkdir sub &&
	echo A >sub/file1 &&
	cp sub/file1 file1 &&
	git add sub/file1 &&
	echo B >sub/file1 &&
	git diff >patch.file &&
	rm sub/file1 &&
	rmdir sub
'

test_expect_success 'apply git diff with -p2' '
	git apply -p2 patch.file
'

test_done
