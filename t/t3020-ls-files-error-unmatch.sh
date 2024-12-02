#!/bin/sh
#
# Copyright (c) 2006 Carl D. Worth
#

test_description='git ls-files test for --error-unmatch option

This test runs git ls-files --error-unmatch to ensure it correctly
returns an error when a non-existent path is provided on the command
line.
'

. ./test-lib.sh

test_expect_success 'setup' '
	touch foo bar &&
	git update-index --add foo bar &&
	git commit -m "add foo bar"
'

test_expect_success 'git ls-files --error-unmatch should fail with unmatched path.' '
	test_must_fail git ls-files --error-unmatch foo bar-does-not-match
'

test_expect_success 'git ls-files --error-unmatch should succeed with matched paths.' '
	git ls-files --error-unmatch foo bar
'

test_done
