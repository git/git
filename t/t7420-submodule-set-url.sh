#!/bin/sh
#
# Copyright (c) 2019 Denton Liu
#

test_description='Test submodules set-url subcommand

This test verifies that the set-url subcommand of git-submodule is working
as expected.
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

test_expect_success 'setup' '
	git config --global protocol.file.allow always
'

test_expect_success 'submodule config cache setup' '
	mkdir submodule &&
	(
		cd submodule &&
		git init &&
		echo a >file &&
		git add file &&
		git commit -ma
	) &&
	mkdir super &&
	(
		cd super &&
		git init &&
		git submodule add ../submodule &&
		git commit -m "add submodule"
	)
'

test_expect_success 'test submodule set-url' '
	# add a commit and move the submodule (change the url)
	(
		cd submodule &&
		echo b >>file &&
		git add file &&
		git commit -mb
	) &&
	mv submodule newsubmodule &&

	git -C newsubmodule show >expect &&
	(
		cd super &&
		test_must_fail git submodule update --remote &&
		git submodule set-url submodule ../newsubmodule &&
		grep -F "url = ../newsubmodule" .gitmodules &&
		git submodule update --remote
	) &&
	git -C super/submodule show >actual &&
	test_cmp expect actual
'

test_done
