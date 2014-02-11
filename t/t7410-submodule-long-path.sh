#!/bin/sh
#
# Copyright (c) 2013 Doug Kelly
#

test_description='Test submodules with a path near PATH_MAX

This test verifies that "git submodule" initialization, update and clones work, including with recursive submodules and paths approaching PATH_MAX (260 characters on Windows)
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

# cloning a submodule calls is_git_directory("$path/../.git/modules/$path"),
# which effectively limits the maximum length to PATH_MAX / 2 minus some
# overhead; start with 3 * 36 = 108 chars (test 2 fails if >= 110)
longpath36=0123456789abcdefghijklmnopqrstuvwxyz
longpath180=$longpath36$longpath36$longpath36$longpath36$longpath36

# the git database must fit within PATH_MAX, which limits the submodule name
# to PATH_MAX - len(pwd) - ~90 (= len("/objects//") + 40-byte sha1 + some
# overhead from the test case)
pwd=$(pwd)
pwdlen=$(echo "$pwd" | wc -c)
longpath=$(echo $longpath180 | cut -c 1-$((170-$pwdlen)))

test_expect_success 'submodule with a long path' '
	git init --bare remote &&
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
		git submodule add ../bundle1 $longpath &&
		test_commit "sogood" &&
		(
			cd $longpath &&
			git rev-parse --verify HEAD >actual &&
			test_cmp ../../../expect actual
		) &&
		git push origin master
	) &&
	mkdir home2 &&
	(
		cd home2 &&
		git clone ../remote test &&
		cd test &&
		git checkout master &&
		git submodule update --init &&
		(
			cd $longpath &&
			git rev-parse --verify HEAD >actual &&
			test_cmp ../../../expect actual
		)
	)
'

test_expect_success 'recursive submodule with a long path' '
	git init --bare super &&
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
		git submodule add ../parent foo &&
		git submodule update --init --recursive
		test_commit "sogood" &&
		(
			cd foo/$longpath &&
			git rev-parse --verify HEAD >actual &&
			test_cmp ../../../../expect actual
		) &&
		git push origin master
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

test_done
