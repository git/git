#!/bin/sh
#
# Copyright (c) 2019 Denton Liu
#

test_description='Test submodules set-branch subcommand

This test verifies that the set-branch subcommand of but-submodule is working
as expected.
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

test_expect_success 'submodule config cache setup' '
	mkdir submodule &&
	(cd submodule &&
		but init &&
		echo a >a &&
		but add . &&
		but cummit -ma &&
		but checkout -b topic &&
		echo b >a &&
		but add . &&
		but cummit -mb
	) &&
	mkdir super &&
	(cd super &&
		but init &&
		but submodule add ../submodule &&
		but cummit -m "add submodule"
	)
'

test_expect_success 'ensure submodule branch is unset' '
	(cd super &&
		! grep branch .butmodules
	)
'

test_expect_success 'test submodule set-branch --branch' '
	(cd super &&
		but submodule set-branch --branch topic submodule &&
		grep "branch = topic" .butmodules &&
		but submodule update --remote &&
		cat <<-\EOF >expect &&
		b
		EOF
		but -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch --default' '
	test_cummit -C submodule c &&
	(cd super &&
		but submodule set-branch --default submodule &&
		! grep branch .butmodules &&
		but submodule update --remote &&
		cat <<-\EOF >expect &&
		c
		EOF
		but -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch -b' '
	test_cummit -C submodule b &&
	(cd super &&
		but submodule set-branch -b topic submodule &&
		grep "branch = topic" .butmodules &&
		but submodule update --remote &&
		cat <<-\EOF >expect &&
		b
		EOF
		but -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'test submodule set-branch -d' '
	test_cummit -C submodule d &&
	(cd super &&
		but submodule set-branch -d submodule &&
		! grep branch .butmodules &&
		but submodule update --remote &&
		cat <<-\EOF >expect &&
		d
		EOF
		but -C submodule show -s --pretty=%s >actual &&
		test_cmp expect actual
	)
'

test_done
