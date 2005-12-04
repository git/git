#!/bin/sh

test_description='git-mv in subdirs'
. ./test-lib.sh

test_expect_success \
    'prepare reference tree' \
    'mkdir path0 path1 &&
     cp ../../COPYING path0/COPYING &&
     git-add path0/COPYING &&
     git-commit -m add -a'

test_expect_success \
    'moving the file' \
    'cd path0 && git-mv COPYING ../path1/COPYING'

# in path0 currently
test_expect_success \
    'commiting the change' \
    'cd .. && git-commit -m move -a'

test_expect_success \
    'checking the commit' \
    'git-diff-tree -r -M --name-status  HEAD^ HEAD | \
    grep -E "^R100.+path0/COPYING.+path1/COPYING"'

test_done
