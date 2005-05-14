#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-update-cache nonsense-path test.

This test creates the following structure in the cache:

    path0       - a file
    path1       - a symlink
    path2/file2 - a file in a directory
    path3/file3 - a file in a directory

and tries to git-update-cache --add the following:

    path0/file0 - a file in a directory
    path1/file1 - a file in a directory
    path2       - a file
    path3       - a symlink

All of the attempts should fail.
'

. ./test-lib.sh

mkdir path2 path3
date >path0
ln -s xyzzy path1
date >path2/file2
date >path3/file3

test_expect_success \
    'git-update-cache --add to add various paths.' \
    'git-update-cache --add -- path0 path1 path2/file2 path3/file3'

rm -fr path?

mkdir path0 path1
date >path2
ln -s frotz path3
date >path0/file0
date >path1/file1

for p in path0/file0 path1/file1 path2 path3
do
	test_expect_failure \
	    "git-update-cache to add conflicting path $p should fail." \
	    "git-update-cache --add -- $p"
done
test_done
