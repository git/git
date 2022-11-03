#!/bin/sh
#
# Copyright (c) 2019 Denton Liu
#

test_description='Test submodules set-branch subcommand

This test verifies that the set-branch subcommand of git-submodule is working
as expected.
'

TEST_PASSES_SANITIZE_LEAK=true
TEST_NO_CREATE_REPO=1
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
		git commit -mb
	) &&
	mkdir super &&
	(cd super &&
		git init &&
		git submodule add ../submodule &&
		git commit -m "add submodule"
	)
'

test_expect_success 'ensure submodule branch is unset' '
	(cd super &&
		! grep branch .gitmodules
	)
'

test_expect_success 'test submodule set-branch --branch' '
	(cd super &&
		git submodule set-branch --branch topic submodule &&
		grep "branch = topic" .gitmodules &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		b
		EOF
		git -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch --default' '
	test_commit -C submodule c &&
	(cd super &&
		git submodule set-branch --default submodule &&
		! grep branch .gitmodules &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		c
		EOF
		git -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch -b' '
	test_commit -C submodule b &&
	(cd super &&
		git submodule set-branch -b topic submodule &&
		grep "branch = topic" .gitmodules &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		b
		EOF
		git -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch -d' '
	test_commit -C submodule d &&
	(cd super &&
		git submodule set-branch -d submodule &&
		! grep branch .gitmodules &&
		git submodule update --remote &&
		cat <<-\EOF >expect &&
		d
		EOF
		git -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_done
