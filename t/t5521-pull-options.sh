#!/bin/sh

test_description='pull options'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir parent &&
	(cd parent && but init &&
	 echo one >file && but add file &&
	 but cummit -m one)
'

test_expect_success 'but pull -q --no-rebase' '
	mkdir clonedq &&
	(cd clonedq && but init &&
	but pull -q --no-rebase "../parent" >out 2>err &&
	test_must_be_empty err &&
	test_must_be_empty out)
'

test_expect_success 'but pull -q --rebase' '
	mkdir clonedqrb &&
	(cd clonedqrb && but init &&
	but pull -q --rebase "../parent" >out 2>err &&
	test_must_be_empty err &&
	test_must_be_empty out &&
	but pull -q --rebase "../parent" >out 2>err &&
	test_must_be_empty err &&
	test_must_be_empty out)
'

test_expect_success 'but pull --no-rebase' '
	mkdir cloned &&
	(cd cloned && but init &&
	but pull --no-rebase "../parent" >out 2>err &&
	test -s err &&
	test_must_be_empty out)
'

test_expect_success 'but pull --rebase' '
	mkdir clonedrb &&
	(cd clonedrb && but init &&
	but pull --rebase "../parent" >out 2>err &&
	test -s err &&
	test_must_be_empty out)
'

test_expect_success 'but pull -v --no-rebase' '
	mkdir clonedv &&
	(cd clonedv && but init &&
	but pull -v --no-rebase "../parent" >out 2>err &&
	test -s err &&
	test_must_be_empty out)
'

test_expect_success 'but pull -v --rebase' '
	mkdir clonedvrb &&
	(cd clonedvrb && but init &&
	but pull -v --rebase "../parent" >out 2>err &&
	test -s err &&
	test_must_be_empty out)
'

test_expect_success 'but pull -v -q --no-rebase' '
	mkdir clonedvq &&
	(cd clonedvq && but init &&
	but pull -v -q --no-rebase "../parent" >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err)
'

test_expect_success 'but pull -q -v --no-rebase' '
	mkdir clonedqv &&
	(cd clonedqv && but init &&
	but pull -q -v --no-rebase "../parent" >out 2>err &&
	test_must_be_empty out &&
	test -s err)
'
test_expect_success 'but pull --cleanup errors early on invalid argument' '
	mkdir clonedcleanup &&
	(cd clonedcleanup && but init &&
	test_must_fail but pull --no-rebase --cleanup invalid "../parent" >out 2>err &&
	test_must_be_empty out &&
	test -s err)
'

test_expect_success 'but pull --no-write-fetch-head fails' '
	mkdir clonedwfh &&
	(cd clonedwfh && but init &&
	test_expect_code 129 but pull --no-write-fetch-head "../parent" >out 2>err &&
	test_must_be_empty out &&
	test_i18ngrep "no-write-fetch-head" err)
'

test_expect_success 'but pull --force' '
	mkdir clonedoldstyle &&
	(cd clonedoldstyle && but init &&
	cat >>.but/config <<-\EOF &&
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
	but pull two &&
	test_cummit A &&
	but branch -f origin &&
	but pull --no-rebase --all --force
	)
'

test_expect_success 'but pull --all' '
	mkdir clonedmulti &&
	(cd clonedmulti && but init &&
	cat >>.but/config <<-\EOF &&
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
	but pull --all
	)
'

test_expect_success 'but pull --dry-run' '
	test_when_finished "rm -rf clonedry" &&
	but init clonedry &&
	(
		cd clonedry &&
		but pull --dry-run ../parent &&
		test_path_is_missing .but/FETCH_HEAD &&
		test_path_is_missing .but/refs/heads/main &&
		test_path_is_missing .but/index &&
		test_path_is_missing file
	)
'

test_expect_success 'but pull --all --dry-run' '
	test_when_finished "rm -rf cloneddry" &&
	but init clonedry &&
	(
		cd clonedry &&
		but remote add origin ../parent &&
		but pull --all --dry-run &&
		test_path_is_missing .but/FETCH_HEAD &&
		test_path_is_missing .but/refs/remotes/origin/main &&
		test_path_is_missing .but/index &&
		test_path_is_missing file
	)
'

test_expect_success 'but pull --allow-unrelated-histories' '
	test_when_finished "rm -fr src dst" &&
	but init src &&
	(
		cd src &&
		test_cummit one &&
		test_cummit two
	) &&
	but clone src dst &&
	(
		cd src &&
		but checkout --orphan side HEAD^ &&
		test_cummit three
	) &&
	(
		cd dst &&
		test_must_fail but pull ../src side &&
		but pull --no-rebase --allow-unrelated-histories ../src side
	)
'

test_expect_success 'but pull does not add a sign-off line' '
	test_when_finished "rm -fr src dst actual" &&
	but init src &&
	test_cummit -C src one &&
	but clone src dst &&
	test_cummit -C src two &&
	but -C dst pull --no-ff &&
	but -C dst show -s --pretty="format:%(trailers)" HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'but pull --no-signoff does not add sign-off line' '
	test_when_finished "rm -fr src dst actual" &&
	but init src &&
	test_cummit -C src one &&
	but clone src dst &&
	test_cummit -C src two &&
	but -C dst pull --no-signoff --no-ff &&
	but -C dst show -s --pretty="format:%(trailers)" HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'but pull --signoff add a sign-off line' '
	test_when_finished "rm -fr src dst expected actual" &&
	echo "Signed-off-by: $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL>" >expected &&
	but init src &&
	test_cummit -C src one &&
	but clone src dst &&
	test_cummit -C src two &&
	but -C dst pull --signoff --no-ff &&
	but -C dst show -s --pretty="format:%(trailers)" HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'but pull --no-signoff flag cancels --signoff flag' '
	test_when_finished "rm -fr src dst actual" &&
	but init src &&
	test_cummit -C src one &&
	but clone src dst &&
	test_cummit -C src two &&
	but -C dst pull --signoff --no-signoff --no-ff &&
	but -C dst show -s --pretty="format:%(trailers)" HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'but pull --no-verify flag passed to merge' '
	test_when_finished "rm -fr src dst actual" &&
	but init src &&
	test_cummit -C src one &&
	but clone src dst &&
	test_hook -C dst cummit-msg <<-\EOF &&
	false
	EOF
	test_cummit -C src two &&
	but -C dst pull --no-ff --no-verify
'

test_expect_success 'but pull --no-verify --verify passed to merge' '
	test_when_finished "rm -fr src dst actual" &&
	but init src &&
	test_cummit -C src one &&
	but clone src dst &&
	test_hook -C dst cummit-msg <<-\EOF &&
	false
	EOF
	test_cummit -C src two &&
	test_must_fail but -C dst pull --no-ff --no-verify --verify
'

test_done
