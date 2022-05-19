#!/bin/sh
#
# Copyright (c) 2006 Carl D. Worth
#

test_description='but ls-files test for --error-unmatch option

This test runs but ls-files --error-unmatch to ensure it correctly
returns an error when a non-existent path is provided on the command
line.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	touch foo bar &&
	but update-index --add foo bar &&
	but cummit -m "add foo bar"
'

test_expect_success \
    'but ls-files --error-unmatch should fail with unmatched path.' \
    'test_must_fail but ls-files --error-unmatch foo bar-does-not-match'

test_expect_success \
    'but ls-files --error-unmatch should succeed with matched paths.' \
    'but ls-files --error-unmatch foo bar'

test_done
