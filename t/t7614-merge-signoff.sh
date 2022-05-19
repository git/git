#!/bin/sh

test_description='but merge --signoff

This test runs but merge --signoff and makes sure that it works.
'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Setup test files
test_setup() {
	# Expected cummit message after merge --signoff
	cat >expected-signed <<EOF &&
Merge branch 'main' into other-branch

Signed-off-by: $(but var BUT_CUMMITTER_IDENT | sed -e "s/>.*/>/")
EOF

	# Expected cummit message after merge without --signoff (or with --no-signoff)
	cat >expected-unsigned <<EOF &&
Merge branch 'main' into other-branch
EOF

	# Initial cummit and feature branch to merge main into it.
	but cummit --allow-empty -m "Initial empty cummit" &&
	but checkout -b other-branch &&
	test_cummit other-branch file1 1
}

# Setup repository, files & feature branch
# This step must be run if You want to test 2,3 or 4
# Order of 2,3,4 is not important, but 1 must be run before
# For example `-r 1,4` or `-r 1,4,2 -v` etc
# But not `-r 2` or `-r 4,3,2,1`
test_expect_success 'setup' '
	test_setup
'

# Test with --signoff flag
test_expect_success 'but merge --signoff adds a sign-off line' '
	but checkout main &&
	test_cummit main-branch-2 file2 2 &&
	but checkout other-branch &&
	but merge main --signoff --no-edit &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'

# Test without --signoff flag
test_expect_success 'but merge does not add a sign-off line' '
	but checkout main &&
	test_cummit main-branch-3 file3 3 &&
	but checkout other-branch &&
	but merge main --no-edit &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-unsigned actual
'

# Test for --no-signoff flag
test_expect_success 'but merge --no-signoff flag cancels --signoff flag' '
	but checkout main &&
	test_cummit main-branch-4 file4 4 &&
	but checkout other-branch &&
	but merge main --no-edit --signoff --no-signoff &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-unsigned actual
'

test_done
