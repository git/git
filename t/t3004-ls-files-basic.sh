#!/bin/sh

test_description='basic ls-files tests

This test runs git ls-files with various unusual or malformed
command-line arguments.
'

. ./test-lib.sh

>empty

test_expect_success 'ls-files in empty repository' '
	git ls-files >actual &&
	test_cmp empty actual
'

test_expect_success 'ls-files with nonexistent path' '
	git ls-files doesnotexist >actual &&
	test_cmp empty actual
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

test_done
