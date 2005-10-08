#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
# Copyright (c) 2005 Robert Fitzsimons
#

test_description='git-ls-tree directory and filenames handling.

This test runs git-ls-tree with the following in a tree.

    1.txt              - a file
    2.txt              - a file
    path0/a/b/c/1.txt  - a file in a directory
    path1/b/c/1.txt    - a file in a directory
    path2/1.txt        - a file in a directory
    path3/1.txt        - a file in a directory
    path3/2.txt        - a file in a directory

Test the handling of mulitple directories which have matching file
entries.  Also test odd filename and missing entries handling.
'
. ./test-lib.sh

test_expect_success \
    'setup' \
    'echo 111 >1.txt &&
     echo 222 >2.txt &&
     mkdir path0 path0/a path0/a/b path0/a/b/c &&
     echo 111 >path0/a/b/c/1.txt &&
     mkdir path1 path1/b path1/b/c &&
     echo 111 >path1/b/c/1.txt &&
     mkdir path2 &&
     echo 111 >path2/1.txt &&
     mkdir path3 &&
     echo 111 >path3/1.txt &&
     echo 222 >path3/2.txt &&
     find *.txt path* \( -type f -o -type l \) -print |
     xargs git-update-index --add &&
     tree=`git-write-tree` &&
     echo $tree'

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"
test_output () {
    sed -e "s/ $_x40	/ X	/" <current >check
    diff -u expected check
}

test_expect_success \
    'ls-tree plain' \
    'git-ls-tree $tree >current &&
     cat >expected <<\EOF &&
100644 blob X	1.txt
100644 blob X	2.txt
040000 tree X	path0
040000 tree X	path1
040000 tree X	path2
040000 tree X	path3
EOF
     test_output'

test_expect_success \
    'ls-tree recursive' \
    'git-ls-tree -r $tree >current &&
     cat >expected <<\EOF &&
100644 blob X	1.txt
100644 blob X	2.txt
040000 tree X	path0
040000 tree X	path0/a
040000 tree X	path0/a/b
040000 tree X	path0/a/b/c
100644 blob X	path0/a/b/c/1.txt
040000 tree X	path1
040000 tree X	path1/b
040000 tree X	path1/b/c
100644 blob X	path1/b/c/1.txt
040000 tree X	path2
100644 blob X	path2/1.txt
040000 tree X	path3
100644 blob X	path3/1.txt
100644 blob X	path3/2.txt
EOF
     test_output'

test_expect_success \
    'ls-tree filter 1.txt' \
    'git-ls-tree $tree 1.txt >current &&
     cat >expected <<\EOF &&
100644 blob X	1.txt
EOF
     test_output'

test_expect_success \
    'ls-tree filter path1/b/c/1.txt' \
    'git-ls-tree $tree path1/b/c/1.txt >current &&
     cat >expected <<\EOF &&
100644 blob X	path1/b/c/1.txt
EOF
     test_output'

test_expect_success \
    'ls-tree filter all 1.txt files' \
    'git-ls-tree $tree 1.txt path0/a/b/c/1.txt path1/b/c/1.txt path2/1.txt path3/1.txt >current &&
     cat >expected <<\EOF &&
100644 blob X	1.txt
100644 blob X	path0/a/b/c/1.txt
100644 blob X	path1/b/c/1.txt
100644 blob X	path2/1.txt
100644 blob X	path3/1.txt
EOF
     test_output'

test_expect_success \
    'ls-tree filter directories' \
    'git-ls-tree $tree path3 path2 path0/a/b/c path1/b/c path0/a >current &&
     cat >expected <<\EOF &&
040000 tree X	path3
100644 blob X	path3/1.txt
100644 blob X	path3/2.txt
040000 tree X	path2
100644 blob X	path2/1.txt
040000 tree X	path0/a/b/c
100644 blob X	path0/a/b/c/1.txt
040000 tree X	path1/b/c
100644 blob X	path1/b/c/1.txt
040000 tree X	path0/a
040000 tree X	path0/a/b
EOF
     test_output'

test_expect_success \
    'ls-tree filter odd names' \
    'git-ls-tree $tree 1.txt /1.txt //1.txt path3/1.txt /path3/1.txt //path3//1.txt path3 /path3/ path3// >current &&
     cat >expected <<\EOF &&
100644 blob X	1.txt
100644 blob X	1.txt
100644 blob X	1.txt
100644 blob X	path3/1.txt
100644 blob X	path3/1.txt
100644 blob X	path3/1.txt
040000 tree X	path3
100644 blob X	path3/1.txt
100644 blob X	path3/2.txt
040000 tree X	path3
100644 blob X	path3/1.txt
100644 blob X	path3/2.txt
040000 tree X	path3
100644 blob X	path3/1.txt
100644 blob X	path3/2.txt
EOF
     test_output'

test_expect_success \
    'ls-tree filter missing files and extra slashes' \
    'git-ls-tree $tree 1.txt/ abc.txt path3//23.txt path3/2.txt/// >current &&
     cat >expected <<\EOF &&
EOF
     test_output'

test_done
