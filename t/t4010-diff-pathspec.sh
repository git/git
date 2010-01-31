#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Pathspec restrictions

Prepare:
        file0
        path1/file1
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh ;# test-lib chdir's into trash

test_expect_success \
    setup \
    'echo frotz >file0 &&
     mkdir path1 &&
     echo rezrov >path1/file1 &&
     git update-index --add file0 path1/file1 &&
     tree=`git write-tree` &&
     echo "$tree" &&
     echo nitfol >file0 &&
     echo yomin >path1/file1 &&
     git update-index file0 path1/file1'

cat >expected <<\EOF
EOF
test_expect_success \
    'limit to path should show nothing' \
    'git diff-index --cached $tree -- path >current &&
     compare_diff_raw current expected'

cat >expected <<\EOF
:100644 100644 766498d93a4b06057a8e49d23f4068f1170ff38f 0a41e115ab61be0328a19b29f18cdcb49338d516 M	path1/file1
EOF
test_expect_success \
    'limit to path1 should show path1/file1' \
    'git diff-index --cached $tree -- path1 >current &&
     compare_diff_raw current expected'

cat >expected <<\EOF
:100644 100644 766498d93a4b06057a8e49d23f4068f1170ff38f 0a41e115ab61be0328a19b29f18cdcb49338d516 M	path1/file1
EOF
test_expect_success \
    'limit to path1/ should show path1/file1' \
    'git diff-index --cached $tree -- path1/ >current &&
     compare_diff_raw current expected'

cat >expected <<\EOF
:100644 100644 766498d93a4b06057a8e49d23f4068f1170ff38f 0a41e115ab61be0328a19b29f18cdcb49338d516 M	file0
EOF
test_expect_success \
    'limit to file0 should show file0' \
    'git diff-index --cached $tree -- file0 >current &&
     compare_diff_raw current expected'

cat >expected <<\EOF
EOF
test_expect_success \
    'limit to file0/ should emit nothing.' \
    'git diff-index --cached $tree -- file0/ >current &&
     compare_diff_raw current expected'

test_expect_success 'diff-tree pathspec' '
	tree2=$(git write-tree) &&
	echo "$tree2" &&
	git diff-tree -r --name-only $tree $tree2 -- pa path1/a >current &&
	>expected &&
	test_cmp expected current
'

test_done
