#!/bin/sh

test_description='basic ls-files tests

This test runs but ls-files with various unusual or malformed
command-line arguments.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'ls-files in empty repository' '
	but ls-files >actual &&
	test_must_be_empty actual
'

test_expect_success 'ls-files with nonexistent path' '
	but ls-files doesnotexist >actual &&
	test_must_be_empty actual
'

test_expect_success 'ls-files with nonsense option' '
	test_expect_code 129 but ls-files --nonsense 2>actual &&
	test_i18ngrep "[Uu]sage: but ls-files" actual
'

test_expect_success 'ls-files -h in corrupt repository' '
	mkdir broken &&
	(
		cd broken &&
		but init &&
		>.but/index &&
		test_expect_code 129 but ls-files -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage: but ls-files " broken/usage
'

test_expect_success SYMLINKS 'ls-files with absolute paths to symlinks' '
	mkdir subs &&
	ln -s nosuch link &&
	ln -s ../nosuch subs/link &&
	but add link subs/link &&
	but ls-files -s link subs/link >expect &&
	but ls-files -s "$(pwd)/link" "$(pwd)/subs/link" >actual &&
	test_cmp expect actual &&

	(
		cd subs &&
		but ls-files -s link >../expect &&
		but ls-files -s "$(pwd)/link" >../actual
	) &&
	test_cmp expect actual
'

test_done
