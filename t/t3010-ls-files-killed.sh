#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-ls-files -k flag test.

This test prepares the following in the cache:

    path0       - a file
    path1       - a symlink
    path2/file2 - a file in a directory
    path3/file3 - a file in a directory

and the following on the filesystem:

    path0/file0 - a file in a directory
    path1/file1 - a file in a directory
    path2       - a file
    path3       - a symlink
    path4	- a file
    path5	- a symlink
    path6/file6 - a file in a directory

git-ls-files -k should report that existing filesystem
objects except path4, path5 and path6/file6 to be killed.
'
. ./test-lib.sh

date >path0
ln -s xyzzy path1
mkdir path2 path3
date >path2/file2
date >path3/file3
test_expect_success \
    'git-update-index --add to add various paths.' \
    "git-update-index --add -- path0 path1 path?/file?"

rm -fr path?
date >path2
ln -s frotz path3
ln -s nitfol path5
mkdir path0 path1 path6
date >path0/file0
date >path1/file1
date >path6/file6

test_expect_success \
    'git-ls-files -k to show killed files.' \
    'git-ls-files -k >.output'
cat >.expected <<EOF
path0/file0
path1/file1
path2
path3
EOF

test_expect_success \
    'validate git-ls-files -k output.' \
    'diff .output .expected'
test_done
