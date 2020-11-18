#!/bin/sh

test_description='git update-index --assume-unchanged test.
'

. ./test-lib.sh

test_expect_success 'setup' '
	: >file &&
	git add file &&
	git commit -m initial &&
	git branch other &&
	echo upstream >file &&
	git add file &&
	git commit -m upstream
'

test_expect_success 'do not switch branches with dirty file' '
	git reset --hard &&
	git checkout other &&
	echo dirt >file &&
	git update-index --assume-unchanged file &&
	test_must_fail git checkout - 2>err &&
	test_i18ngrep overwritten err
'

test_done
