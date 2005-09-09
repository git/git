#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='git branch --foo should not create bogus branch

This test runs git branch --help and checks that the argument is properly
handled.  Specifically, that a bogus branch is not created.
'
. ./test-lib.sh

test_expect_success \
    'prepare an trivial repository' \
    'echo Hello > A &&
     git-update-index --add A &&
     git-commit -m "Initial commit."'

test_expect_failure \
    'git branch --help should return error code' \
    'git-branch --help'

test_expect_failure \
    'git branch --help should not have created a bogus branch' \
    'test -f .git/refs/heads/--help'

test_done
