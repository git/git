#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-checkout-index test.

This test registers the following filesystem structure in the cache:

    path0/file0	- a file in a directory
    path1/file1 - a file in a directory

and attempts to check it out when the work tree has:

    path0/file0 - a file in a directory
    path1       - a symlink pointing at "path0"

Checkout cache should fail to extract path1/file1 because the leading
path path1 is occupied by a non-directory.  With "-f" it should remove
the symlink path1 and create directory path1 and file path1/file1.
'
. ./test-lib.sh

show_files() {
	# show filesystem files, just [-dl] for type and name
	find path? -ls |
	sed -e 's/^[0-9]* * [0-9]* * \([-bcdl]\)[^ ]* *[0-9]* *[^ ]* *[^ ]* *[0-9]* [A-Z][a-z][a-z] [0-9][0-9] [^ ]* /fs: \1 /'
	# what's in the cache, just mode and name
	git-ls-files --stage |
	sed -e 's/^\([0-9]*\) [0-9a-f]* [0-3] /ca: \1 /'
	# what's in the tree, just mode and name.
	git-ls-tree -r "$1" |
	sed -e 's/^\([0-9]*\)	[^ ]*	[0-9a-f]*	/tr: \1 /'
}

mkdir path0
date >path0/file0
test_expect_success \
    'git-update-index --add path0/file0' \
    'git-update-index --add path0/file0'
test_expect_success \
    'writing tree out with git-write-tree' \
    'tree1=$(git-write-tree)'
test_debug 'show_files $tree1'

mkdir path1
date >path1/file1
test_expect_success \
    'git-update-index --add path1/file1' \
    'git-update-index --add path1/file1'
test_expect_success \
    'writing tree out with git-write-tree' \
    'tree2=$(git-write-tree)'
test_debug 'show_files $tree2'

rm -fr path1
test_expect_success \
    'read previously written tree and checkout.' \
    'git-read-tree -m $tree1 && git-checkout-index -f -a'
test_debug 'show_files $tree1'

ln -s path0 path1
test_expect_success \
    'git-update-index --add a symlink.' \
    'git-update-index --add path1'
test_expect_success \
    'writing tree out with git-write-tree' \
    'tree3=$(git-write-tree)'
test_debug 'show_files $tree3'

# Morten says "Got that?" here.
# Test begins.

test_expect_success \
    'read previously written tree and checkout.' \
    'git-read-tree $tree2 && git-checkout-index -f -a'
test_debug 'show_files $tree2'

test_expect_success \
    'checking out conflicting path with -f' \
    'test ! -h path0 && test -d path0 &&
     test ! -h path1 && test -d path1 &&
     test ! -h path0/file0 && test -f path0/file0 &&
     test ! -h path1/file1 && test -f path1/file1'

test_done
