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
'
. ./test-lib.sh

date >path0
ln -s xyzzy path1
mkdir path2 path3
date >path2/file2
date >path2-junk
date >path3/file3
date >path3-junk
git update-index --add path3-junk path3/file3

cat >expected1 <<EOF
expected1
expected2
output
path0
path1
path2-junk
path2/file2
EOF
sed -e 's|path2/file2|path2/|' <expected1 >expected2

test_expect_success \
    'git ls-files --others to show output.' \
    'git ls-files --others >output'

test_expect_success \
    'git ls-files --others should pick up symlinks.' \
    'diff output expected1'

test_expect_success \
    'git ls-files --others --directory to show output.' \
    'git ls-files --others --directory >output'


test_expect_success \
    'git ls-files --others --directory should not get confused.' \
    'diff output expected2'

test_done
