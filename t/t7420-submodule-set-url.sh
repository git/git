#!/bin/sh
#
# Copyright (c) 2019 Denton Liu
#

test_description='Test submodules set-url subcommand

This test verifies that the set-url subcommand of but-submodule is working
as expected.
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

test_expect_success 'submodule config cache setup' '
	mkdir submodule &&
	(
		cd submodule &&
		but init &&
		echo a >file &&
		but add file &&
		but cummit -ma
	) &&
	mkdir super &&
	(
		cd super &&
		but init &&
		but submodule add ../submodule &&
		but cummit -m "add submodule"
	)
'

test_expect_success 'test submodule set-url' '
	# add a cummit and move the submodule (change the url)
	(
		cd submodule &&
		echo b >>file &&
		but add file &&
		but cummit -mb
	) &&
	mv submodule newsubmodule &&

	but -C newsubmodule show >expect &&
	(
		cd super &&
		test_must_fail but submodule update --remote &&
		but submodule set-url submodule ../newsubmodule &&
		grep -F "url = ../newsubmodule" .butmodules &&
		but submodule update --remote
	) &&
	but -C super/submodule show >actual &&
	test_cmp expect actual
'

test_done
