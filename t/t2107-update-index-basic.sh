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
	test_i18ngrep "[Uu]sage: git update-index" err
'

test_expect_success 'update-index -h with corrupt index' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		>.git/index &&
		test_expect_code 129 git update-index -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage: git update-index" broken/usage
'

test_expect_success '--cacheinfo does not accept blob null sha1' '
	echo content >file &&
	git add file &&
	git rev-parse :file >expect &&
	test_must_fail git update-index --cacheinfo 100644 $_z40 file &&
	git rev-parse :file >actual &&
	test_cmp expect actual
'

test_expect_success '--cacheinfo does not accept gitlink null sha1' '
	git init submodule &&
	(cd submodule && test_commit foo) &&
	git add submodule &&
	git rev-parse :submodule >expect &&
	test_must_fail git update-index --cacheinfo 160000 $_z40 submodule &&
	git rev-parse :submodule >actual &&
	test_cmp expect actual
'

test_done
