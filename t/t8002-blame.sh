#!/bin/sh

test_description='git blame'
. ./test-lib.sh

PROG='git blame -c'
. "$TEST_DIRECTORY"/annotate-tests.sh

PROG='git blame -c -e'
test_expect_success 'blame --show-email' '
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

find_blame () {
	sed -e 's/^[^(]*//'
}

test_expect_success 'blame with no options and no config' '
	git blame one >blame &&
	find_blame <blame >result &&
	test_cmp expected_n result
'

test_expect_success 'blame with showemail options' '
	git blame --show-email one >blame1 &&
	find_blame <blame1 >result &&
	test_cmp expected_e result &&
	git blame -e one >blame2 &&
	find_blame <blame2 >result &&
	test_cmp expected_e result &&
	git blame --no-show-email one >blame3 &&
	find_blame <blame3 >result &&
	test_cmp expected_n result
'

test_expect_success 'blame with showEmail config false' '
	git config blame.showEmail false &&
	git blame one >blame1 &&
	find_blame <blame1 >result &&
	test_cmp expected_n result &&
	git blame --show-email one >blame2 &&
	find_blame <blame2 >result &&
	test_cmp expected_e result &&
	git blame -e one >blame3 &&
	find_blame <blame3 >result &&
	test_cmp expected_e result &&
	git blame --no-show-email one >blame4 &&
	find_blame <blame4 >result &&
	test_cmp expected_n result
'

test_expect_success 'blame with showEmail config true' '
	git config blame.showEmail true &&
	git blame one >blame1 &&
	find_blame <blame1 >result &&
	test_cmp expected_e result &&
	git blame --no-show-email one >blame2 &&
	find_blame <blame2 >result &&
	test_cmp expected_n result
'

test_done
