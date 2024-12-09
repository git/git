#!/bin/sh
#
# Copyright (c) 2019 Denton Liu
#

test_description='Test submodules set-branch subcommand

This test verifies that the set-branch subcommand of git-submodule is working
as expected.
'

TEST_NO_CREATE_REPO=1

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	git config --global protocol.file.allow always
'

test_expect_success 'submodule config cache setup' '
	mkdir submodule &&
	(cd submodule &&
		git init &&
		echo a >a &&
		git add . &&
		git commit -ma &&
		git checkout -b topic &&
		echo b >a &&
		git add . &&
		git commit -mb &&
		git checkout main
	) &&
	mkdir super &&
	(cd super &&
		git init &&
		git submodule add ../submodule &&
		git submodule add --name thename ../submodule thepath &&
		git commit -m "add submodules"
	)
'

test_expect_success 'ensure submodule branch is unset' '
	(cd super &&
		test_cmp_config "" -f .gitmodules --default "" submodule.submodule.branch
	)
'

test_expect_success 'test submodule set-branch --branch' '
	(cd super &&
		git submodule set-branch --branch topic submodule &&
		test_cmp_config topic -f .gitmodules submodule.submodule.branch &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		b
		EOF
		git -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch --default' '
	(cd super &&
		git submodule set-branch --default submodule &&
		test_cmp_config "" -f .gitmodules --default "" submodule.submodule.branch &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		a
		EOF
		git -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch -b' '
	(cd super &&
		git submodule set-branch -b topic submodule &&
		test_cmp_config topic -f .gitmodules submodule.submodule.branch &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		b
		EOF
		git -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch -d' '
	(cd super &&
		git submodule set-branch -d submodule &&
		test_cmp_config "" -f .gitmodules --default "" submodule.submodule.branch &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		a
		EOF
		git -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch --branch with named submodule' '
	(cd super &&
		git submodule set-branch --branch topic thepath &&
		test_cmp_config topic -f .gitmodules submodule.thename.branch &&
		test_cmp_config "" -f .gitmodules --default "" submodule.thepath.branch &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		b
		EOF
		git -C thepath show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch --default with named submodule' '
	(cd super &&
		git submodule set-branch --default thepath &&
		test_cmp_config "" -f .gitmodules --default "" submodule.thename.branch &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		a
		EOF
		git -C thepath show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_done
