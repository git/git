#!/bin/sh

test_description='git merge --signoff

This test runs git merge --signoff and make sure that it works.
'

. ./test-lib.sh

# A simple files to commit
cat >file1 <<EOF
1
EOF

cat >file2 <<EOF
2
EOF

cat >file3 <<EOF
3
EOF

# Expected commit message after merge --signoff
cat >expected-signed <<EOF
Merge branch 'master' into other-branch

Signed-off-by: $(git var GIT_COMMITTER_IDENT | sed -e "s/>.*/>/")
EOF

# Expected commit message after merge without --signoff (or with --no-signoff)
cat >expected-unsigned <<EOF
Merge branch 'master' into other-branch
EOF


# We configure an alias to do the merge --signoff so that
# on the next subtest we can show that --no-signoff overrides the alias
test_expect_success 'merge --signoff adds a sign-off line' '
	git commit --allow-empty -m "Initial empty commit" &&
  git checkout -b other-branch &&
	git add file1 && git commit -m other-branch &&
  git checkout master &&
	git add file2 && git commit -m master-branch &&
  git checkout other-branch &&
  git config alias.msob "merge --signoff --no-edit" &&
	git msob master &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	test_cmp expected-signed actual
'

test_expect_success 'master --no-signoff does not add a sign-off line' '
	git checkout master &&
  git add file3 && git commit -m master-branch-2 &&
  git checkout other-branch &&
	git msob --no-signoff master &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	test_cmp expected-unsigned actual
'

test_done
