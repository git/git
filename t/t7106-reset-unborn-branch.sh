#!/bin/sh

test_description='but reset should work on unborn branch'
. ./test-lib.sh

test_expect_success 'setup' '
	echo a >a &&
	echo b >b
'

test_expect_success 'reset' '
	but add a b &&
	but reset &&

	but ls-files >actual &&
	test_must_be_empty actual
'

test_expect_success 'reset HEAD' '
	rm .but/index &&
	but add a b &&
	test_must_fail but reset HEAD
'

test_expect_success 'reset $file' '
	rm .but/index &&
	but add a b &&
	but reset a &&

	echo b >expect &&
	but ls-files >actual &&
	test_cmp expect actual
'

test_expect_success PERL 'reset -p' '
	rm .but/index &&
	but add a &&
	echo y >yes &&
	but reset -p <yes >output &&

	but ls-files >actual &&
	test_must_be_empty actual &&
	test_i18ngrep "Unstage" output
'

test_expect_success 'reset --soft is a no-op' '
	rm .but/index &&
	but add a &&
	but reset --soft &&

	echo a >expect &&
	but ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'reset --hard' '
	rm .but/index &&
	but add a &&
	test_when_finished "echo a >a" &&
	but reset --hard &&

	but ls-files >actual &&
	test_must_be_empty actual &&
	test_path_is_missing a
'

test_done
