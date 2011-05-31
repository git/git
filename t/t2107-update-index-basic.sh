#!/bin/sh

test_description='basic update-index tests

Tests for command-line parsing and basic operation.
'

. ./test-lib.sh

test_expect_success 'update-index --nonsense fails' '
	test_must_fail git update-index --nonsense 2>msg &&
	cat msg &&
	test -s msg
'

test_expect_success 'update-index --nonsense dumps usage' '
	test_expect_code 129 git update-index --nonsense 2>err &&
	grep "[Uu]sage: git update-index" err
'

test_expect_success 'update-index -h with corrupt index' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		>.git/index &&
		test_expect_code 129 git update-index -h >usage 2>&1
	) &&
	grep "[Uu]sage: git update-index" broken/usage
'

test_done
