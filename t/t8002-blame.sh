#!/bin/sh

test_description='git sleuth'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_CREATE_REPO_NO_TEMPLATE=1
. ./test-lib.sh

PROG='git sleuth -c'
. "$TEST_DIRECTORY"/annotate-tests.sh

test_expect_success 'setup' '
	hexsz=$(test_oid hexsz)
'

test_expect_success 'sleuth untracked file in empty repo' '
	>untracked &&
	test_must_fail git sleuth untracked
'

PROG='git sleuth -c -e'
test_expect_success 'sleuth --show-email' '
	check_count \
		"<A@test.git>" 1 \
		"<B@test.git>" 1 \
		"<B1@test.git>" 1 \
		"<B2@test.git>" 1 \
		"<author@example.com>" 1 \
		"<C@test.git>" 1 \
		"<D@test.git>" 1 \
		"<E at test dot git>" 1
'

test_expect_success 'setup showEmail tests' '
	echo "bin: test number 1" >one &&
	git add one &&
	GIT_AUTHOR_NAME=name1 \
	GIT_AUTHOR_EMAIL=email1@test.git \
	git commit -m First --date="2010-01-01 01:00:00" &&
	cat >expected_n <<-\EOF &&
	(name1 2010-01-01 01:00:00 +0000 1) bin: test number 1
	EOF
	cat >expected_e <<-\EOF
	(<email1@test.git> 2010-01-01 01:00:00 +0000 1) bin: test number 1
	EOF
'

find_sleuth () {
	sed -e 's/^[^(]*//'
}

test_expect_success 'sleuth with no options and no config' '
	git sleuth one >sleuth &&
	find_sleuth <sleuth >result &&
	test_cmp expected_n result
'

test_expect_success 'sleuth with showemail options' '
	git sleuth --show-email one >sleuth1 &&
	find_sleuth <sleuth1 >result &&
	test_cmp expected_e result &&
	git sleuth -e one >sleuth2 &&
	find_sleuth <sleuth2 >result &&
	test_cmp expected_e result &&
	git sleuth --no-show-email one >sleuth3 &&
	find_sleuth <sleuth3 >result &&
	test_cmp expected_n result
'

test_expect_success 'sleuth with showEmail config false' '
	git config sleuth.showEmail false &&
	git sleuth one >sleuth1 &&
	find_sleuth <sleuth1 >result &&
	test_cmp expected_n result &&
	git sleuth --show-email one >sleuth2 &&
	find_sleuth <sleuth2 >result &&
	test_cmp expected_e result &&
	git sleuth -e one >sleuth3 &&
	find_sleuth <sleuth3 >result &&
	test_cmp expected_e result &&
	git sleuth --no-show-email one >sleuth4 &&
	find_sleuth <sleuth4 >result &&
	test_cmp expected_n result
'

test_expect_success 'sleuth with showEmail config true' '
	git config sleuth.showEmail true &&
	git sleuth one >sleuth1 &&
	find_sleuth <sleuth1 >result &&
	test_cmp expected_e result &&
	git sleuth --no-show-email one >sleuth2 &&
	find_sleuth <sleuth2 >result &&
	test_cmp expected_n result
'

test_expect_success 'set up abbrev tests' '
	test_commit abbrev &&
	sha1=$(git rev-parse --verify HEAD) &&
	check_abbrev () {
		expect=$1 && shift &&
		echo $sha1 | cut -c 1-$expect >expect &&
		git sleuth "$@" abbrev.t >actual &&
		perl -lne "/[0-9a-f]+/ and print \$&" <actual >actual.sha &&
		test_cmp expect actual.sha
	}
'

test_expect_success 'sleuth --abbrev=<n> works' '
	# non-boundary commits get +1 for alignment
	check_abbrev 31 --abbrev=30 HEAD &&
	check_abbrev 30 --abbrev=30 ^HEAD
'

test_expect_success 'sleuth -l aligns regular and boundary commits' '
	check_abbrev $hexsz         -l HEAD &&
	check_abbrev $((hexsz - 1)) -l ^HEAD
'

test_expect_success 'sleuth --abbrev with full length behaves like -l' '
	check_abbrev $hexsz         --abbrev=$hexsz HEAD &&
	check_abbrev $((hexsz - 1)) --abbrev=$hexsz ^HEAD
'

test_expect_success '--no-abbrev works like --abbrev with full length' '
	check_abbrev $hexsz --no-abbrev
'

test_expect_success '--exclude-promisor-objects does not BUG-crash' '
	test_must_fail git sleuth --exclude-promisor-objects one
'

test_expect_success 'sleuth with uncommitted edits in partial clone does not crash' '
	git init server &&
	echo foo >server/file.txt &&
	git -C server add file.txt &&
	git -C server commit -m file &&

	git clone --filter=blob:none "file://$(pwd)/server" client &&
	echo bar >>client/file.txt &&
	git -C client sleuth file.txt
'

test_done
