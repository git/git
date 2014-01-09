#!/bin/sh
#
# Copyright (c) 2013 Doug Kelly
#

test_description='Test submodules with a path near PATH_MAX

This test verifies that "git submodule" initialization, update and clones work, including with recursive submodules and paths approaching PATH_MAX (260 characters on Windows)
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

longpath=""
for (( i=0; i<4; i++ )); do
	longpath="0123456789abcdefghijklmnopqrstuvwxyz$longpath"
done
# Pick a substring maximum of 90 characters
# This should be good, since we'll add on a lot for temp directories
longpath=${longpath:0:90}; export longpath

test_expect_failure 'submodule with a long path' '
	git config --global protocol.file.allow always &&
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	git -c init.defaultBranch=long init --bare remote &&
	test_create_repo bundle1 &&
	(
		cd bundle1 &&
		test_commit "shoot" &&
		git rev-parse --verify HEAD >../expect
	) &&
	mkdir home &&
	(
		cd home &&
		git clone ../remote test &&
		cd test &&
		git checkout -B long &&
		git submodule add ../bundle1 $longpath &&
		test_commit "sogood" &&
		(
			cd $longpath &&
			git rev-parse --verify HEAD >actual &&
			test_cmp ../../../expect actual
		) &&
		git push origin long
	) &&
	mkdir home2 &&
	(
		cd home2 &&
		git clone ../remote test &&
		cd test &&
		git checkout long &&
		git submodule update --init &&
		(
			cd $longpath &&
			git rev-parse --verify HEAD >actual &&
			test_cmp ../../../expect actual
		)
	)
'

test_expect_failure 'recursive submodule with a long path' '
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= \
	git -c init.defaultBranch=long init --bare super &&
	test_create_repo child &&
	(
		cd child &&
		test_commit "shoot" &&
		git rev-parse --verify HEAD >../expect
	) &&
	test_create_repo parent &&
	(
		cd parent &&
		git submodule add ../child $longpath &&
		test_commit "aim"
	) &&
	mkdir home3 &&
	(
		cd home3 &&
		git clone ../super test &&
		cd test &&
		git checkout -B long &&
		git submodule add ../parent foo &&
		git submodule update --init --recursive &&
		test_commit "sogood" &&
		(
			cd foo/$longpath &&
			git rev-parse --verify HEAD >actual &&
			test_cmp ../../../../expect actual
		) &&
		git push origin long
	) &&
	mkdir home4 &&
	(
		cd home4 &&
		git clone ../super test --recursive &&
		(
			cd test/foo/$longpath &&
			git rev-parse --verify HEAD >actual &&
			test_cmp ../../../../expect actual
		)
	)
'
unset longpath

test_done
