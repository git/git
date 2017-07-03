#!/bin/sh

test_description='git merge --signoff

This test runs git merge --signoff and makes sure that it works.
'

. ./test-lib.sh

# Setup test files
test_setup() {
	# A simples files to commit
	echo "1" >file1
	echo "2" >file2
	echo "3" >file3
	echo "4" >file4

	# Expected commit message after merge --signoff
	cat >expected-signed <<EOF
Merge branch 'master' into other-branch

Signed-off-by: $(git var GIT_COMMITTER_IDENT | sed -e "s/>.*/>/")
EOF

	# Expected commit message after merge without --signoff (or with --no-signoff)
	cat >expected-unsigned <<EOF
Merge branch 'master' into other-branch
EOF

	# Initial commit and feature branch to merge master into it.
	git commit --allow-empty -m "Initial empty commit"
	git checkout -b other-branch
	git add file1
	git commit -m other-branch
}

# Setup repository, files & feature branch
test_expect_success 'setup' '
	test_setup
'

# Test with --signoff flag
test_expect_success 'git merge --signoff adds a sign-off line' '
	git checkout master &&
	git add file2 &&
	git commit -m master-branch &&
	git checkout other-branch &&
	git merge master --signoff --no-edit &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'

# Test without --signoff flag
test_expect_success 'git merge does not add a sign-off line' '
	git checkout master &&
	git add file3 &&
	git commit -m master-branch-2 &&
	git checkout other-branch &&
	git merge master --no-edit &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-unsigned actual
'

# Test for --no-signoff flag
test_expect_success 'git merge --no-signoff flag cancels --signoff flag' '
	git checkout master &&
	git add file4 &&
	git commit -m master-branch-3 &&
	git checkout other-branch &&
	git merge master --no-edit --signoff --no-signoff &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-unsigned actual
'

test_done
