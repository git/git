#!/bin/sh

test_description='git command aliasing'

. ./test-lib.sh

test_expect_success 'nested aliases - internal execution' '
	git config alias.nested-internal-1 nested-internal-2 &&
	git config alias.nested-internal-2 status &&
	git nested-internal-1 >output &&
	test_i18ngrep "^On branch " output
'

test_expect_success 'nested aliases - mixed execution' '
	git config alias.nested-external-1 nested-external-2 &&
	git config alias.nested-external-2 "!git nested-external-3" &&
	git config alias.nested-external-3 status &&
	git nested-external-1 >output &&
	test_i18ngrep "^On branch " output
'

test_expect_success 'looping aliases - internal execution' '
	git config alias.loop-internal-1 loop-internal-2 &&
	git config alias.loop-internal-2 loop-internal-3 &&
	git config alias.loop-internal-3 loop-internal-2 &&
	test_must_fail git loop-internal-1 2>output &&
	test_i18ngrep "^fatal: alias loop detected: expansion of" output
'

# This test is disabled until external loops are fixed, because would block
# the test suite for a full minute.
#
#test_expect_failure 'looping aliases - mixed execution' '
#	git config alias.loop-mixed-1 loop-mixed-2 &&
#	git config alias.loop-mixed-2 "!git loop-mixed-1" &&
#	test_must_fail git loop-mixed-1 2>output &&
#	test_i18ngrep "^fatal: alias loop detected: expansion of" output
#'

test_done
