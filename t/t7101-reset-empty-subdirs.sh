#!/bin/sh
#
# Copyright (c) 2006 Shawn Pearce
#

test_description='git reset should cull empty subdirs'
. ./test-lib.sh

test_expect_success 'creating initial files' '
     mkdir path0 &&
     cp "$TEST_DIRECTORY"/../COPYING path0/COPYING &&
     git add path0/COPYING &&
     git commit -m add -a
'

test_expect_success 'creating second files' '
     mkdir path1 &&
     mkdir path1/path2 &&
     cp "$TEST_DIRECTORY"/../COPYING path1/path2/COPYING &&
     cp "$TEST_DIRECTORY"/../COPYING path1/COPYING &&
     cp "$TEST_DIRECTORY"/../COPYING COPYING &&
     cp "$TEST_DIRECTORY"/../COPYING path0/COPYING-TOO &&
     git add path1/path2/COPYING &&
     git add path1/COPYING &&
     git add COPYING &&
     git add path0/COPYING-TOO &&
     git commit -m change -a
'

test_expect_success 'resetting tree HEAD^' '
     git reset --hard HEAD^
'

test_expect_success 'checking initial files exist after rewind' '
     test -d path0 &&
     test -f path0/COPYING
'

test_expect_success 'checking lack of path1/path2/COPYING' '
    ! test -f path1/path2/COPYING
'

test_expect_success 'checking lack of path1/COPYING' '
    ! test -f path1/COPYING
'

test_expect_success 'checking lack of COPYING' '
     ! test -f COPYING
'

test_expect_success 'checking checking lack of path1/COPYING-TOO' '
     ! test -f path0/COPYING-TOO
'

test_expect_success 'checking lack of path1/path2' '
     ! test -d path1/path2
'

test_expect_success 'checking lack of path1' '
     ! test -d path1
'

test_done
