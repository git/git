#!/bin/sh

test_description='basic ls-files tests

This test runs git ls-files with various unusual or malformed
command-line arguments.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'ls-files in empty repository' '
	git ls-files >actual &&
	test_must_be_empty actual
'

test_expect_success 'ls-files with nonexistent path' '
	git ls-files doesnotexist >actual &&
	test_must_be_empty actual
'

test_expect_success 'ls-files with nonsense option' '
	test_expect_code 129 git ls-files --nonsense 2>actual &&
	test_i18ngrep "[Uu]sage: git ls-files" actual
'

test_expect_success 'ls-files -h in corrupt repository' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		>.git/index &&
		test_expect_code 129 git ls-files -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage: git ls-files " broken/usage
'

test_expect_success SYMLINKS 'ls-files with absolute paths to symlinks' '
	mkdir subs &&
	ln -s nosuch link &&
	ln -s ../nosuch subs/link &&
	git add link subs/link &&
	git ls-files -s link subs/link >expect &&
	git ls-files -s "$(pwd)/link" "$(pwd)/subs/link" >actual &&
	test_cmp expect actual &&

	(
		cd subs &&
		git ls-files -s link >../expect &&
		git ls-files -s "$(pwd)/link" >../actual
	) &&
	test_cmp expect actual
'

test_done
