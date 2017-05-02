#!/bin/sh

test_description='checkout can handle submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-submodule-update.sh

test_expect_success 'setup' '
	mkdir submodule &&
	(cd submodule &&
	 git init &&
	 test_commit first) &&
	git add submodule &&
	test_tick &&
	git commit -m superproject &&
	(cd submodule &&
	 test_commit second) &&
	git add submodule &&
	test_tick &&
	git commit -m updated.superproject
'

test_expect_success '"reset <submodule>" updates the index' '
	git update-index --refresh &&
	git diff-files --quiet &&
	git diff-index --quiet --cached HEAD &&
	git reset HEAD^ submodule &&
	test_must_fail git diff-files --quiet &&
	git reset submodule &&
	git diff-files --quiet
'

test_expect_success '"checkout <submodule>" updates the index only' '
	git update-index --refresh &&
	git diff-files --quiet &&
	git diff-index --quiet --cached HEAD &&
	git checkout HEAD^ submodule &&
	test_must_fail git diff-files --quiet &&
	git checkout HEAD submodule &&
	git diff-files --quiet
'

test_expect_success '"checkout <submodule>" honors diff.ignoreSubmodules' '
	git config diff.ignoreSubmodules dirty &&
	echo x> submodule/untracked &&
	git checkout HEAD >actual 2>&1 &&
	! test -s actual
'

test_expect_success '"checkout <submodule>" honors submodule.*.ignore from .gitmodules' '
	git config diff.ignoreSubmodules none &&
	git config -f .gitmodules submodule.submodule.path submodule &&
	git config -f .gitmodules submodule.submodule.ignore untracked &&
	git checkout HEAD >actual 2>&1 &&
	! test -s actual
'

test_expect_success '"checkout <submodule>" honors submodule.*.ignore from .git/config' '
	git config -f .gitmodules submodule.submodule.ignore none &&
	git config submodule.submodule.path submodule &&
	git config submodule.submodule.ignore all &&
	git checkout HEAD >actual 2>&1 &&
	! test -s actual
'

KNOWN_FAILURE_DIRECTORY_SUBMODULE_CONFLICTS=1
test_submodule_switch_recursing "git checkout --recurse-submodules"

test_submodule_forced_switch_recursing "git checkout -f --recurse-submodules"

test_submodule_switch "git checkout"

test_submodule_forced_switch "git checkout -f"

test_done
