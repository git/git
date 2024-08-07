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
	mkdir namedsubmodule &&
	(
		cd namedsubmodule &&
		git init &&
		echo 1 >file &&
		git add file &&
		git commit -m1
	) &&
	mkdir super &&
	(
		cd super &&
		git init &&
		git submodule add ../submodule &&
		git submodule add --name thename ../namedsubmodule thepath &&
		git commit -m "add submodules"
	)
'

test_expect_success 'test submodule set-url' '
	# add commits and move the submodules (change the urls)
	(
		cd submodule &&
		echo b >>file &&
		git add file &&
		git commit -mb
	) &&
	mv submodule newsubmodule &&

	(
		cd namedsubmodule &&
		echo 2 >>file &&
		git add file &&
		git commit -m2
	) &&
	mv namedsubmodule newnamedsubmodule &&

	git -C newsubmodule show >expect &&
	git -C newnamedsubmodule show >>expect &&
	(
		cd super &&
		test_must_fail git submodule update --remote &&
		git submodule set-url submodule ../newsubmodule &&
		test_cmp_config ../newsubmodule -f .gitmodules submodule.submodule.url &&
		git submodule set-url thepath ../newnamedsubmodule &&
		test_cmp_config ../newnamedsubmodule -f .gitmodules submodule.thename.url &&
		test_cmp_config "" -f .gitmodules --default "" submodule.thepath.url &&
		git submodule update --remote
	) &&
	git -C super/submodule show >actual &&
	git -C super/thepath show >>actual &&
	test_cmp expect actual
'

test_done
