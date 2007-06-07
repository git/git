#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-checkout-index test.

This test registers the following filesystem structure in the
cache:

    path0       - a file
    path1/file1 - a file in a directory

And then tries to checkout in a work tree that has the following:

    path0/file0 - a file in a directory
    path1       - a file

The git-checkout-index command should fail when attempting to checkout
path0, finding it is occupied by a directory, and path1/file1, finding
path1 is occupied by a non-directory.  With "-f" flag, it should remove
the conflicting paths and succeed.
'
. ./test-lib.sh

date >path0
mkdir path1
date >path1/file1

test_expect_success \
    'git-update-index --add various paths.' \
    'git-update-index --add path0 path1/file1'

rm -fr path0 path1
mkdir path0
date >path0/file0
date >path1

test_expect_failure \
    'git-checkout-index without -f should fail on conflicting work tree.' \
    'git-checkout-index -a'

test_expect_success \
    'git-checkout-index with -f should succeed.' \
    'git-checkout-index -f -a'

test_expect_success \
    'git-checkout-index conflicting paths.' \
    'test -f path0 && test -d path1 && test -f path1/file1'

test_done
