#!/bin/sh

test_description='pull options'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir parent &&
	(cd parent && git init &&
	 echo one >file && git add file &&
	 git commit -m one)
'

test_expect_success 'git pull -q --no-rebase' '
	mkdir clonedq &&
	(cd clonedq && git init &&
	git pull -q --no-rebase "../parent" >out 2>err &&
	test_must_be_empty err &&
	test_must_be_empty out)
'

test_expect_success 'git pull -q --rebase' '
	mkdir clonedqrb &&
	(cd clonedqrb && git init &&
	git pull -q --rebase "../parent" >out 2>err &&
	test_must_be_empty err &&
	test_must_be_empty out &&
	git pull -q --rebase "../parent" >out 2>err &&
	test_must_be_empty err &&
	test_must_be_empty out)
'

test_expect_success 'git pull --no-rebase' '
	mkdir cloned &&
	(cd cloned && git init &&
	git pull --no-rebase "../parent" >out 2>err &&
	test -s err &&
	test_must_be_empty out)
'

test_expect_success 'git pull --rebase' '
	mkdir clonedrb &&
	(cd clonedrb && git init &&
	git pull --rebase "../parent" >out 2>err &&
	test -s err &&
	test_must_be_empty out)
'

test_expect_success 'git pull -v --no-rebase' '
	mkdir clonedv &&
	(cd clonedv && git init &&
	git pull -v --no-rebase "../parent" >out 2>err &&
	test -s err &&
	test_must_be_empty out)
'

test_expect_success 'git pull -v --rebase' '
	mkdir clonedvrb &&
	(cd clonedvrb && git init &&
	git pull -v --rebase "../parent" >out 2>err &&
	test -s err &&
	test_must_be_empty out)
'

test_expect_success 'git pull -v -q --no-rebase' '
	mkdir clonedvq &&
	(cd clonedvq && git init &&
	git pull -v -q --no-rebase "../parent" >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err)
'

test_expect_success 'git pull -q -v --no-rebase' '
	mkdir clonedqv &&
	(cd clonedqv && git init &&
	git pull -q -v --no-rebase "../parent" >out 2>err &&
	test_must_be_empty out &&
	test -s err)
'
test_expect_success 'git pull --cleanup errors early on invalid argument' '
	mkdir clonedcleanup &&
	(cd clonedcleanup && git init &&
	test_must_fail git pull --no-rebase --cleanup invalid "../parent" >out 2>err &&
	test_must_be_empty out &&
	test -s err)
'

test_expect_success 'git pull --no-write-fetch-head fails' '
	mkdir clonedwfh &&
	(cd clonedwfh && git init &&
	test_expect_code 129 git pull --no-write-fetch-head "../parent" >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep "no-write-fetch-head" err)
'

test_expect_success 'git pull --force' '
	mkdir clonedoldstyle &&
	(cd clonedoldstyle && git init &&
	cat >>.git/config <<-\EOF &&
	[remote "one"]
		url = ../parent
		fetch = refs/heads/main:refs/heads/mirror
	[remote "two"]
		url = ../parent
		fetch = refs/heads/main:refs/heads/origin
	[branch "main"]
		remote = two
		merge = refs/heads/main
	EOF
	git pull two &&
	test_commit A &&
	git branch -f origin &&
	git pull --no-rebase --all --force
	)
'

test_expect_success 'git pull --all' '
	mkdir clonedmulti &&
	(cd clonedmulti && git init &&
	cat >>.git/config <<-\EOF &&
	[remote "one"]
		url = ../parent
		fetch = refs/heads/*:refs/remotes/one/*
	[remote "two"]
		url = ../parent
		fetch = refs/heads/*:refs/remotes/two/*
	[branch "main"]
		remote = one
		merge = refs/heads/main
	EOF
	git pull --all
	)
'

test_expect_success 'git pull --dry-run' '
	test_when_finished "rm -rf clonedry" &&
	git init clonedry &&
	(
		cd clonedry &&
		git pull --dry-run ../parent &&
		test_path_is_missing .git/FETCH_HEAD &&
		test_ref_missing refs/heads/main &&
		test_path_is_missing .git/index &&
		test_path_is_missing file
	)
'

test_expect_success 'git pull --all --dry-run' '
	test_when_finished "rm -rf cloneddry" &&
	git init clonedry &&
	(
		cd clonedry &&
		git remote add origin ../parent &&
		git pull --all --dry-run &&
		test_path_is_missing .git/FETCH_HEAD &&
		test_ref_missing refs/remotes/origin/main &&
		test_path_is_missing .git/index &&
		test_path_is_missing file
	)
'

test_expect_success 'git pull --allow-unrelated-histories' '
	test_when_finished "rm -fr src dst" &&
	git init src &&
	(
		cd src &&
		test_commit one &&
		test_commit two
	) &&
	git clone src dst &&
	(
		cd src &&
		git checkout --orphan side HEAD^ &&
		test_commit three
	) &&
	(
		cd dst &&
		test_must_fail git pull ../src side &&
		git pull --no-rebase --allow-unrelated-histories ../src side
	)
'

test_expect_success 'git pull does not add a sign-off line' '
	test_when_finished "rm -fr src dst actual" &&
	git init src &&
	test_commit -C src one &&
	git clone src dst &&
	test_commit -C src two &&
	git -C dst pull --no-ff &&
	git -C dst show -s --pretty="format:%(trailers)" HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'git pull --no-signoff does not add sign-off line' '
	test_when_finished "rm -fr src dst actual" &&
	git init src &&
	test_commit -C src one &&
	git clone src dst &&
	test_commit -C src two &&
	git -C dst pull --no-signoff --no-ff &&
	git -C dst show -s --pretty="format:%(trailers)" HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'git pull --signoff add a sign-off line' '
	test_when_finished "rm -fr src dst expected actual" &&
	echo "Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" >expected &&
	git init src &&
	test_commit -C src one &&
	git clone src dst &&
	test_commit -C src two &&
	git -C dst pull --signoff --no-ff &&
	git -C dst show -s --pretty="format:%(trailers)" HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'git pull --no-signoff flag cancels --signoff flag' '
	test_when_finished "rm -fr src dst actual" &&
	git init src &&
	test_commit -C src one &&
	git clone src dst &&
	test_commit -C src two &&
	git -C dst pull --signoff --no-signoff --no-ff &&
	git -C dst show -s --pretty="format:%(trailers)" HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'git pull --no-verify flag passed to merge' '
	test_when_finished "rm -fr src dst actual" &&
	git init src &&
	test_commit -C src one &&
	git clone src dst &&
	test_hook -C dst commit-msg <<-\EOF &&
	false
	EOF
	test_commit -C src two &&
	git -C dst pull --no-ff --no-verify
'

test_expect_success 'git pull --no-verify --verify passed to merge' '
	test_when_finished "rm -fr src dst actual" &&
	git init src &&
	test_commit -C src one &&
	git clone src dst &&
	test_hook -C dst commit-msg <<-\EOF &&
	false
	EOF
	test_commit -C src two &&
	test_must_fail git -C dst pull --no-ff --no-verify --verify
'

test_done
