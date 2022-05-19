#!/bin/sh
#
# Copyright (c) 2008 David Aguilar
#

test_description='but submodule sync

These tests exercise the "but submodule sync" subcommand.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo file >file &&
	but add file &&
	test_tick &&
	but cummit -m upstream &&
	but clone . super &&
	but clone super submodule &&
	(
		cd submodule &&
		but submodule add ../submodule sub-submodule &&
		test_tick &&
		but cummit -m "sub-submodule"
	) &&
	(
		cd super &&
		but submodule add ../submodule submodule &&
		test_tick &&
		but cummit -m "submodule"
	) &&
	but clone super super-clone &&
	(
		cd super-clone &&
		but submodule update --init --recursive
	) &&
	but clone super empty-clone &&
	(
		cd empty-clone &&
		but submodule init
	) &&
	but clone super top-only-clone &&
	but clone super relative-clone &&
	(
		cd relative-clone &&
		but submodule update --init --recursive
	) &&
	but clone super recursive-clone &&
	(
		cd recursive-clone &&
		but submodule update --init --recursive
	)
'

test_expect_success 'change submodule' '
	(
		cd submodule &&
		echo second line >>file &&
		test_tick &&
		but cummit -a -m "change submodule"
	)
'

reset_submodule_urls () {
	(
		root=$(pwd) &&
		cd super-clone/submodule &&
		but config remote.origin.url "$root/submodule"
	) &&
	(
		root=$(pwd) &&
		cd super-clone/submodule/sub-submodule &&
		but config remote.origin.url "$root/submodule"
	)
}

test_expect_success 'change submodule url' '
	(
		cd super &&
		cd submodule &&
		but checkout main &&
		but pull
	) &&
	mv submodule moved-submodule &&
	(
		cd moved-submodule &&
		but config -f .butmodules submodule.sub-submodule.url ../moved-submodule &&
		test_tick &&
		but cummit -a -m moved-sub-submodule
	) &&
	(
		cd super &&
		but config -f .butmodules submodule.submodule.url ../moved-submodule &&
		test_tick &&
		but cummit -a -m moved-submodule
	)
'

test_expect_success '"but submodule sync" should update submodule URLs' '
	(
		cd super-clone &&
		but pull --no-recurse-submodules &&
		but submodule sync
	) &&
	test -d "$(
		cd super-clone/submodule &&
		but config remote.origin.url
	)" &&
	test ! -d "$(
		cd super-clone/submodule/sub-submodule &&
		but config remote.origin.url
	)" &&
	(
		cd super-clone/submodule &&
		but checkout main &&
		but pull
	) &&
	(
		cd super-clone &&
		test -d "$(but config submodule.submodule.url)"
	)
'

test_expect_success '"but submodule sync --recursive" should update all submodule URLs' '
	(
		cd super-clone &&
		(
			cd submodule &&
			but pull --no-recurse-submodules
		) &&
		but submodule sync --recursive
	) &&
	test -d "$(
		cd super-clone/submodule &&
		but config remote.origin.url
	)" &&
	test -d "$(
		cd super-clone/submodule/sub-submodule &&
		but config remote.origin.url
	)" &&
	(
		cd super-clone/submodule/sub-submodule &&
		but checkout main &&
		but pull
	)
'

test_expect_success 'reset submodule URLs' '
	reset_submodule_urls super-clone
'

test_expect_success '"but submodule sync" should update submodule URLs - subdirectory' '
	(
		cd super-clone &&
		but pull --no-recurse-submodules &&
		mkdir -p sub &&
		cd sub &&
		but submodule sync >../../output
	) &&
	test_i18ngrep "\\.\\./submodule" output &&
	test -d "$(
		cd super-clone/submodule &&
		but config remote.origin.url
	)" &&
	test ! -d "$(
		cd super-clone/submodule/sub-submodule &&
		but config remote.origin.url
	)" &&
	(
		cd super-clone/submodule &&
		but checkout main &&
		but pull
	) &&
	(
		cd super-clone &&
		test -d "$(but config submodule.submodule.url)"
	)
'

test_expect_success '"but submodule sync --recursive" should update all submodule URLs - subdirectory' '
	(
		cd super-clone &&
		(
			cd submodule &&
			but pull --no-recurse-submodules
		) &&
		mkdir -p sub &&
		cd sub &&
		but submodule sync --recursive >../../output
	) &&
	test_i18ngrep "\\.\\./submodule/sub-submodule" output &&
	test -d "$(
		cd super-clone/submodule &&
		but config remote.origin.url
	)" &&
	test -d "$(
		cd super-clone/submodule/sub-submodule &&
		but config remote.origin.url
	)" &&
	(
		cd super-clone/submodule/sub-submodule &&
		but checkout main &&
		but pull
	)
'

test_expect_success '"but submodule sync" should update known submodule URLs' '
	(
		cd empty-clone &&
		but pull &&
		but submodule sync &&
		test -d "$(but config submodule.submodule.url)"
	)
'

test_expect_success '"but submodule sync" should not vivify uninteresting submodule' '
	(
		cd top-only-clone &&
		but pull &&
		but submodule sync &&
		test -z "$(but config submodule.submodule.url)" &&
		but submodule sync submodule &&
		test -z "$(but config submodule.submodule.url)"
	)
'

test_expect_success '"but submodule sync" handles origin URL of the form foo' '
	(
		cd relative-clone &&
		but remote set-url origin foo &&
		but submodule sync &&
		(
			cd submodule &&
			#actual fails with: "cannot strip off url foo
			test "$(but config remote.origin.url)" = "../submodule"
		)
	)
'

test_expect_success '"but submodule sync" handles origin URL of the form foo/bar' '
	(
		cd relative-clone &&
		but remote set-url origin foo/bar &&
		but submodule sync &&
		(
			cd submodule &&
			#actual foo/submodule
			test "$(but config remote.origin.url)" = "../foo/submodule"
		) &&
		(
			cd submodule/sub-submodule &&
			test "$(but config remote.origin.url)" != "../../foo/submodule"
		)
	)
'

test_expect_success '"but submodule sync --recursive" propagates changes in origin' '
	(
		cd recursive-clone &&
		but remote set-url origin foo/bar &&
		but submodule sync --recursive &&
		(
			cd submodule &&
			#actual foo/submodule
			test "$(but config remote.origin.url)" = "../foo/submodule"
		) &&
		(
			cd submodule/sub-submodule &&
			test "$(but config remote.origin.url)" = "../../foo/submodule"
		)
	)
'

test_expect_success '"but submodule sync" handles origin URL of the form ./foo' '
	(
		cd relative-clone &&
		but remote set-url origin ./foo &&
		but submodule sync &&
		(
			cd submodule &&
			#actual ./submodule
			test "$(but config remote.origin.url)" = "../submodule"
		)
	)
'

test_expect_success '"but submodule sync" handles origin URL of the form ./foo/bar' '
	(
		cd relative-clone &&
		but remote set-url origin ./foo/bar &&
		but submodule sync &&
		(
			cd submodule &&
			#actual ./foo/submodule
			test "$(but config remote.origin.url)" = "../foo/submodule"
		)
	)
'

test_expect_success '"but submodule sync" handles origin URL of the form ../foo' '
	(
		cd relative-clone &&
		but remote set-url origin ../foo &&
		but submodule sync &&
		(
			cd submodule &&
			#actual ../submodule
			test "$(but config remote.origin.url)" = "../../submodule"
		)
	)
'

test_expect_success '"but submodule sync" handles origin URL of the form ../foo/bar' '
	(
		cd relative-clone &&
		but remote set-url origin ../foo/bar &&
		but submodule sync &&
		(
			cd submodule &&
			#actual ../foo/submodule
			test "$(but config remote.origin.url)" = "../../foo/submodule"
		)
	)
'

test_expect_success '"but submodule sync" handles origin URL of the form ../foo/bar with deeply nested submodule' '
	(
		cd relative-clone &&
		but remote set-url origin ../foo/bar &&
		mkdir -p a/b/c &&
		(
			cd a/b/c &&
			but init &&
			>.butignore &&
			but add .butignore &&
			test_tick &&
			but cummit -m "initial cummit"
		) &&
		but submodule add ../bar/a/b/c ./a/b/c &&
		but submodule sync &&
		(
			cd a/b/c &&
			#actual ../foo/bar/a/b/c
			test "$(but config remote.origin.url)" = "../../../../foo/bar/a/b/c"
		)
	)
'


test_done
