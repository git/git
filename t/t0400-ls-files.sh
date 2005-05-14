#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-ls-files test (--others should pick up symlinks).

This test runs git-ls-files --others with the following on the
filesystem.

    path0       - a file
    path1	- a symlink
    path2/file2 - a file in a directory
'
. ./test-lib.sh

date >path0
ln -s xyzzy path1
mkdir path2
date >path2/file2
test_expect_success \
    'git-ls-files --others to show output.' \
    'git-ls-files --others >.output'
cat >.expected <<EOF
path0
path1
path2/file2
EOF

test_expect_success \
    'git-ls-files --others should pick up symlinks.' \
    'diff .output .expected'
test_done
