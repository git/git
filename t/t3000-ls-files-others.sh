#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git ls-files test (--others should pick up symlinks).

This test runs git ls-files --others with the following on the
filesystem.

    path0       - a file
    path1	- a symlink
    path2/file2 - a file in a directory
    path3-junk  - a file to confuse things
    path3/file3 - a file in a directory
    path4       - an empty directory
'
. ./test-lib.sh

test_expect_success 'setup ' '
	date >path0 &&
	if test_have_prereq SYMLINKS
	then
		ln -s xyzzy path1
	else
		date >path1
	fi &&
	mkdir path2 path3 path4 &&
	date >path2/file2 &&
	date >path2-junk &&
	date >path3/file3 &&
	date >path3-junk &&
	git update-index --add path3-junk path3/file3
'

test_expect_success 'setup: expected output' '
	cat >expected1 <<-\EOF &&
	expected1
	expected2
	expected3
	output
	path0
	path1
	path2-junk
	path2/file2
	EOF

	sed -e "s|path2/file2|path2/|" <expected1 >expected2 &&
	cp expected2 expected3 &&
	echo path4/ >>expected2
'

test_expect_success 'ls-files --others' '
	git ls-files --others >output &&
	test_cmp expected1 output
'

test_expect_success 'ls-files --others --directory' '
	git ls-files --others --directory >output &&
	test_cmp expected2 output
'

test_expect_success '--no-empty-directory hides empty directory' '
	git ls-files --others --directory --no-empty-directory >output &&
	test_cmp expected3 output
'

test_done
