#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-ls-tree test.

This test runs git-ls-tree with the following in a tree.

    path0       - a file
    path1	- a symlink
    path2/foo   - a file in a directory
    path2/bazbo - a symlink in a directory
    path2/baz/b - a file in a directory in a directory

The new path restriction code should do the right thing for path2 and
path2/baz
'
. ./test-lib.sh

test_expect_success \
    'setup' \
    'mkdir path2 path2/baz &&
     echo Hi >path0 &&
     ln -s path0 path1 &&
     echo Lo >path2/foo &&
     ln -s ../path1 path2/bazbo &&
     echo Mi >path2/baz/b &&
     find path? \( -type f -o -type l \) -print |
     xargs git-update-cache --add &&
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
100644 blob X	path0
120000 blob X	path1
040000 tree X	path2
EOF
     test_output'

test_expect_success \
    'ls-tree recursive' \
    'git-ls-tree -r $tree >current &&
     cat >expected <<\EOF &&
100644 blob X	path0
120000 blob X	path1
040000 tree X	path2
040000 tree X	path2/baz
100644 blob X	path2/baz/b
120000 blob X	path2/bazbo
100644 blob X	path2/foo
EOF
     test_output'

test_expect_success \
    'ls-tree filtered' \
    'git-ls-tree $tree path >current &&
     cat >expected <<\EOF &&
EOF
     test_output'


test_expect_success \
    'ls-tree filtered' \
    'git-ls-tree $tree path1 path0 >current &&
     cat >expected <<\EOF &&
100644 blob X	path0
120000 blob X	path1
EOF
     test_output'

test_expect_success \
    'ls-tree filtered' \
    'git-ls-tree $tree path2 >current &&
     cat >expected <<\EOF &&
040000 tree X	path2
040000 tree X	path2/baz
100644 blob X	path2/baz/b
120000 blob X	path2/bazbo
100644 blob X	path2/foo
EOF
     test_output'

test_expect_success \
    'ls-tree filtered' \
    'git-ls-tree $tree path2/baz >current &&
     cat >expected <<\EOF &&
040000 tree X	path2/baz
100644 blob X	path2/baz/b
EOF
     test_output'

test_done
