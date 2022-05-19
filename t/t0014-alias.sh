#!/bin/sh

test_description='but command aliasing'

. ./test-lib.sh

test_expect_success 'nested aliases - internal execution' '
	but config alias.nested-internal-1 nested-internal-2 &&
	but config alias.nested-internal-2 status &&
	but nested-internal-1 >output &&
	test_i18ngrep "^On branch " output
'

test_expect_success 'nested aliases - mixed execution' '
	but config alias.nested-external-1 nested-external-2 &&
	but config alias.nested-external-2 "!but nested-external-3" &&
	but config alias.nested-external-3 status &&
	but nested-external-1 >output &&
	test_i18ngrep "^On branch " output
'

test_expect_success 'looping aliases - internal execution' '
	but config alias.loop-internal-1 loop-internal-2 &&
	but config alias.loop-internal-2 loop-internal-3 &&
	but config alias.loop-internal-3 loop-internal-2 &&
	test_must_fail but loop-internal-1 2>output &&
	test_i18ngrep "^fatal: alias loop detected: expansion of" output
'

# This test is disabled until external loops are fixed, because would block
# the test suite for a full minute.
#
#test_expect_failure 'looping aliases - mixed execution' '
#	but config alias.loop-mixed-1 loop-mixed-2 &&
#	but config alias.loop-mixed-2 "!but loop-mixed-1" &&
#	test_must_fail but loop-mixed-1 2>output &&
#	test_i18ngrep "^fatal: alias loop detected: expansion of" output
#'

test_expect_success 'run-command formats empty args properly' '
    test_must_fail env BUT_TRACE=1 but frotz a "" b " " c 2>actual.raw &&
    sed -ne "/run_command:/s/.*trace: run_command: //p" actual.raw >actual &&
    echo "but-frotz a '\'''\'' b '\'' '\'' c" >expect &&
    test_cmp expect actual
'

test_done
