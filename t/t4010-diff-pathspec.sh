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
     tree=$(git write-tree) &&
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
:100644 100644 766498d93a4b06057a8e49d23f4068f1170ff38f 0a41e115ab61be0328a19b29f18cdcb49338d516 M	path1/file1
EOF
test_expect_success \
    '"*file1" should show path1/file1' \
    'git diff-index --cached $tree -- "*file1" >current &&
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

EMPTY_TREE=4b825dc642cb6eb9a060e54bf8d69288fbee4904

test_expect_success 'diff-tree with wildcard shows dir also matches' '
	git diff-tree --name-only $EMPTY_TREE $tree -- "f*" >result &&
	echo file0 >expected &&
	test_cmp expected result
'

test_expect_success 'diff-tree -r with wildcard' '
	git diff-tree -r --name-only $EMPTY_TREE $tree -- "*file1" >result &&
	echo path1/file1 >expected &&
	test_cmp expected result
'

test_expect_success 'diff-tree with wildcard shows dir also matches' '
	git diff-tree --name-only $tree $tree2 -- "path1/f*" >result &&
	echo path1 >expected &&
	test_cmp expected result
'

test_expect_success 'diff-tree -r with wildcard from beginning' '
	git diff-tree -r --name-only $tree $tree2 -- "path1/*file1" >result &&
	echo path1/file1 >expected &&
	test_cmp expected result
'

test_expect_success 'diff-tree -r with wildcard' '
	git diff-tree -r --name-only $tree $tree2 -- "path1/f*" >result &&
	echo path1/file1 >expected &&
	test_cmp expected result
'

test_expect_success 'setup submodules' '
	test_tick &&
	git init submod &&
	( cd submod && test_commit first; ) &&
	git add submod &&
	git commit -m first &&
	( cd submod && test_commit second; ) &&
	git add submod &&
	git commit -m second
'

test_expect_success 'diff-tree ignores trailing slash on submodule path' '
	git diff --name-only HEAD^ HEAD submod >expect &&
	git diff --name-only HEAD^ HEAD submod/ >actual &&
	test_cmp expect actual
'

test_expect_success 'diff multiple wildcard pathspecs' '
	mkdir path2 &&
	echo rezrov >path2/file1 &&
	git update-index --add path2/file1 &&
	tree3=$(git write-tree) &&
	git diff --name-only $tree $tree3 -- "path2*1" "path1*1" >actual &&
	cat <<-\EOF >expect &&
	path1/file1
	path2/file1
	EOF
	test_cmp expect actual
'

test_expect_success 'diff-cache ignores trailing slash on submodule path' '
	git diff --name-only HEAD^ submod >expect &&
	git diff --name-only HEAD^ submod/ >actual &&
	test_cmp expect actual
'

test_done
