#!/bin/sh
#
# Copyright (c) 2006 Carl D. Worth
#

test_description='Test of the various options to git-rm.'

. ./test-lib.sh

# Setup some files to be removed
touch foo bar
git-add foo bar
# Need one to test --
touch -- -q
git update-index --add -- -q
git-commit -m "add foo, bar, and -q"

test_expect_success \
    'Pre-check that foo is in index before git-rm foo' \
    'git-ls-files --error-unmatch foo'

test_expect_success \
    'Test that git-rm foo succeeds' \
    'git-rm foo'

test_expect_failure \
    'Post-check that foo is not in index after git-rm foo' \
    'git-ls-files --error-unmatch foo'

test_expect_success \
    'Test that "git-rm -f bar" works' \
    'git-rm -f bar'

test_expect_failure \
    'Post-check that bar no longer exists' \
    '[ -f bar ]'

test_expect_success \
    'Test that "git-rm -- -q" works to delete a file named -q' \
    'git-rm -- -q'

test_done
