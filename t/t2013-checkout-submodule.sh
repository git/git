#!/bin/sh

test_description='checkout can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

test_expect_success 'setup' '
	mkdir submodule &&
	(cd submodule &&
	 but init &&
	 test_cummit first) &&
	but add submodule &&
	test_tick &&
	but cummit -m superproject &&
	(cd submodule &&
	 test_cummit second) &&
	but add submodule &&
	test_tick &&
	but cummit -m updated.superproject
'

test_expect_success '"reset <submodule>" updates the index' '
	but update-index --refresh &&
	but diff-files --quiet &&
	but diff-index --quiet --cached HEAD &&
	but reset HEAD^ submodule &&
	test_must_fail but diff-files --quiet &&
	but reset submodule &&
	but diff-files --quiet
'

test_expect_success '"checkout <submodule>" updates the index only' '
	but update-index --refresh &&
	but diff-files --quiet &&
	but diff-index --quiet --cached HEAD &&
	but checkout HEAD^ submodule &&
	test_must_fail but diff-files --quiet &&
	but checkout HEAD submodule &&
	but diff-files --quiet
'

test_expect_success '"checkout <submodule>" honors diff.ignoreSubmodules' '
	but config diff.ignoreSubmodules dirty &&
	echo x> submodule/untracked &&
	but checkout HEAD >actual 2>&1 &&
	test_must_be_empty actual
'

test_expect_success '"checkout <submodule>" honors submodule.*.ignore from .butmodules' '
	but config diff.ignoreSubmodules none &&
	but config -f .butmodules submodule.submodule.path submodule &&
	but config -f .butmodules submodule.submodule.ignore untracked &&
	but checkout HEAD >actual 2>&1 &&
	test_must_be_empty actual
'

test_expect_success '"checkout <submodule>" honors submodule.*.ignore from .but/config' '
	but config -f .butmodules submodule.submodule.ignore none &&
	but config submodule.submodule.path submodule &&
	but config submodule.submodule.ignore all &&
	but checkout HEAD >actual 2>&1 &&
	test_must_be_empty actual
'

KNOWN_FAILURE_DIRECTORY_SUBMODULE_CONFLICTS=1
test_submodule_switch_recursing_with_args "checkout"

test_submodule_forced_switch_recursing_with_args "checkout -f"

test_submodule_switch "checkout"

test_submodule_forced_switch "checkout -f"

test_done
