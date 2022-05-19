#!/bin/sh

test_description='but blame'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

PROG='but blame -c'
. "$TEST_DIRECTORY"/annotate-tests.sh

test_expect_success 'setup' '
	hexsz=$(test_oid hexsz)
'

test_expect_success 'blame untracked file in empty repo' '
	>untracked &&
	test_must_fail but blame untracked
'

PROG='but blame -c -e'
test_expect_success 'blame --show-email' '
	check_count \
		"<A@test.but>" 1 \
		"<B@test.but>" 1 \
		"<B1@test.but>" 1 \
		"<B2@test.but>" 1 \
		"<author@example.com>" 1 \
		"<C@test.but>" 1 \
		"<D@test.but>" 1 \
		"<E at test dot but>" 1
'

test_expect_success 'setup showEmail tests' '
	echo "bin: test number 1" >one &&
	but add one &&
	BUT_AUTHOR_NAME=name1 \
	BUT_AUTHOR_EMAIL=email1@test.but \
	but cummit -m First --date="2010-01-01 01:00:00" &&
	cat >expected_n <<-\EOF &&
	(name1 2010-01-01 01:00:00 +0000 1) bin: test number 1
	EOF
	cat >expected_e <<-\EOF
	(<email1@test.but> 2010-01-01 01:00:00 +0000 1) bin: test number 1
	EOF
'

find_blame () {
	sed -e 's/^[^(]*//'
}

test_expect_success 'blame with no options and no config' '
	but blame one >blame &&
	find_blame <blame >result &&
	test_cmp expected_n result
'

test_expect_success 'blame with showemail options' '
	but blame --show-email one >blame1 &&
	find_blame <blame1 >result &&
	test_cmp expected_e result &&
	but blame -e one >blame2 &&
	find_blame <blame2 >result &&
	test_cmp expected_e result &&
	but blame --no-show-email one >blame3 &&
	find_blame <blame3 >result &&
	test_cmp expected_n result
'

test_expect_success 'blame with showEmail config false' '
	but config blame.showEmail false &&
	but blame one >blame1 &&
	find_blame <blame1 >result &&
	test_cmp expected_n result &&
	but blame --show-email one >blame2 &&
	find_blame <blame2 >result &&
	test_cmp expected_e result &&
	but blame -e one >blame3 &&
	find_blame <blame3 >result &&
	test_cmp expected_e result &&
	but blame --no-show-email one >blame4 &&
	find_blame <blame4 >result &&
	test_cmp expected_n result
'

test_expect_success 'blame with showEmail config true' '
	but config blame.showEmail true &&
	but blame one >blame1 &&
	find_blame <blame1 >result &&
	test_cmp expected_e result &&
	but blame --no-show-email one >blame2 &&
	find_blame <blame2 >result &&
	test_cmp expected_n result
'

test_expect_success 'set up abbrev tests' '
	test_cummit abbrev &&
	sha1=$(but rev-parse --verify HEAD) &&
	check_abbrev () {
		expect=$1 && shift &&
		echo $sha1 | cut -c 1-$expect >expect &&
		but blame "$@" abbrev.t >actual &&
		perl -lne "/[0-9a-f]+/ and print \$&" <actual >actual.sha &&
		test_cmp expect actual.sha
	}
'

test_expect_success 'blame --abbrev=<n> works' '
	# non-boundary cummits get +1 for alignment
	check_abbrev 31 --abbrev=30 HEAD &&
	check_abbrev 30 --abbrev=30 ^HEAD
'

test_expect_success 'blame -l aligns regular and boundary cummits' '
	check_abbrev $hexsz         -l HEAD &&
	check_abbrev $((hexsz - 1)) -l ^HEAD
'

test_expect_success 'blame --abbrev with full length behaves like -l' '
	check_abbrev $hexsz         --abbrev=$hexsz HEAD &&
	check_abbrev $((hexsz - 1)) --abbrev=$hexsz ^HEAD
'

test_expect_success '--no-abbrev works like --abbrev with full length' '
	check_abbrev $hexsz --no-abbrev
'

test_expect_success '--exclude-promisor-objects does not BUG-crash' '
	test_must_fail but blame --exclude-promisor-objects one
'

test_expect_success 'blame with uncummitted edits in partial clone does not crash' '
	but init server &&
	echo foo >server/file.txt &&
	but -C server add file.txt &&
	but -C server cummit -m file &&

	but clone --filter=blob:none "file://$(pwd)/server" client &&
	echo bar >>client/file.txt &&
	but -C client blame file.txt
'

test_done
