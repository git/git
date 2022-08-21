#!/bin/sh

test_description='git reset should work on unborn branch'
. ./test-lib.sh

test_expect_success 'setup' '
	echo a >a &&
	echo b >b
'

test_expect_success 'reset' '
	git add a b &&
	git reset &&

	git ls-files >actual &&
	test_must_be_empty actual
'

test_expect_success 'reset HEAD' '
	rm .git/index &&
	git add a b &&
	test_must_fail git reset HEAD
'

test_expect_success 'reset $file' '
	rm .git/index &&
	git add a b &&
	git reset a &&

	echo b >expect &&
	git ls-files >actual &&
	test_cmp expect actual
'

test_expect_success PERL 'reset -p' '
	rm .git/index &&
	git add a &&
	echo y >yes &&
	git reset -p <yes >output &&

	git ls-files >actual &&
	test_must_be_empty actual &&
	test_i18ngrep "Unstage" output
'

test_expect_success 'reset --soft is a no-op' '
	rm .git/index &&
	git add a &&
	git reset --soft &&

	echo a >expect &&
	git ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'reset --hard' '
	rm .git/index &&
	git add a &&
	test_when_finished "echo a >a" &&
	git reset --hard &&

	git ls-files >actual &&
	test_must_be_empty actual &&
	test_path_is_missing a
'

test_done
