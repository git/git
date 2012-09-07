#!/bin/sh

test_description='basic checkout-index tests
'

. ./test-lib.sh

test_expect_success 'checkout-index --gobbledegook' '
	test_expect_code 129 git checkout-index --gobbledegook 2>err &&
	test_i18ngrep "[Uu]sage" err
'

test_expect_success 'checkout-index -h in broken repository' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		>.git/index &&
		test_expect_code 129 git checkout-index -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_done
