#!/bin/sh

test_description='checkout can handle submodules'

. ./test-lib.sh

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
	test_must_fail git reset HEAD^ submodule &&
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

test_done
