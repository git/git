#!/bin/sh
#
# Copyright (c) 2006 Carl D. Worth
#

test_description='Test of git-add, including the -- option.'

. ./test-lib.sh

test_expect_success \
    'Test of git-add' \
    'touch foo && git-add foo'

test_expect_success \
    'Post-check that foo is in the index' \
    'git-ls-files foo | grep foo'

test_expect_success \
    'Test that "git-add -- -q" works' \
    'touch -- -q && git-add -- -q'

test_done
