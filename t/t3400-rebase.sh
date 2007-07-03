#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='git rebase should not destroy author information

This test runs git rebase and checks that the author information is not lost.
'
. ./test-lib.sh

export GIT_AUTHOR_EMAIL=bogus_email_address

test_expect_success \
    'prepare repository with topic branch, then rebase against master' \
    'echo First > A &&
     git update-index --add A &&
     git-commit -m "Add A." &&
     git checkout -b my-topic-branch &&
     echo Second > B &&
     git update-index --add B &&
     git-commit -m "Add B." &&
     git checkout -f master &&
     echo Third >> A &&
     git update-index A &&
     git-commit -m "Modify A." &&
     git checkout -f my-topic-branch &&
     git rebase master'

test_expect_failure \
    'the rebase operation should not have destroyed author information' \
    'git log | grep "Author:" | grep "<>"'

test_done
