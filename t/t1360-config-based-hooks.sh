#!/bin/bash

test_description='config-managed multihooks, including git-hook command'

. ./test-lib.sh

test_expect_success 'git hook rejects commands without a mode' '
	test_must_fail git hook pre-commit
'


test_expect_success 'git hook rejects commands without a hookname' '
	test_must_fail git hook list
'

test_expect_success 'setup hooks in global, and local' '
	git config --add --local hook.pre-commit.command "/path/ghi" &&
	git config --add --global hook.pre-commit.command "/path/def"
'

test_expect_success 'git hook list orders by config order' '
	cat >expected <<-\EOF &&
	global:	/path/def
	local:	/path/ghi
	EOF

	git hook list pre-commit >actual &&
	test_cmp expected actual
'

test_expect_success 'git hook list dereferences a hookcmd' '
	git config --add --local hook.pre-commit.command "abc" &&
	git config --add --global hookcmd.abc.command "/path/abc" &&

	cat >expected <<-\EOF &&
	global:	/path/def
	local:	/path/ghi
	local:	/path/abc
	EOF

	git hook list pre-commit >actual &&
	test_cmp expected actual
'

test_expect_success 'git hook list reorders on duplicate commands' '
	git config --add --local hook.pre-commit.command "/path/def" &&

	cat >expected <<-\EOF &&
	local:	/path/ghi
	local:	/path/abc
	local:	/path/def
	EOF

	git hook list pre-commit >actual &&
	test_cmp expected actual
'

test_expect_success 'git hook list --porcelain prints just the command' '
	cat >expected <<-\EOF &&
	/path/ghi
	/path/abc
	/path/def
	EOF

	git hook list --porcelain pre-commit >actual &&
	test_cmp expected actual
'

test_done
