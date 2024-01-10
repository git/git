#!/bin/sh

test_description='git rebase --signoff

This test runs git rebase --signoff and make sure that it works.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# A simple file to commit
cat >file <<EOF
a
EOF

# Expected commit message for initial commit after rebase --signoff
cat >expected-initial-signed <<EOF
Initial empty commit

Signed-off-by: $(git var GIT_COMMITTER_IDENT | sed -e "s/>.*/>/")
EOF

# Expected commit message after rebase --signoff
cat >expected-signed <<EOF
first

Signed-off-by: $(git var GIT_COMMITTER_IDENT | sed -e "s/>.*/>/")
EOF

# Expected commit message after rebase without --signoff (or with --no-signoff)
cat >expected-unsigned <<EOF
first
EOF


# We configure an alias to do the rebase --signoff so that
# on the next subtest we can show that --no-signoff overrides the alias
test_expect_success 'rebase --signoff adds a sign-off line' '
	git commit --allow-empty -m "Initial empty commit" &&
	git add file && git commit -m first &&
	git config alias.rbs "rebase --signoff" &&
	git rbs HEAD^ &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	test_cmp expected-signed actual
'

test_expect_success 'rebase --no-signoff does not add a sign-off line' '
	git commit --amend -m "first" &&
	git rbs --no-signoff HEAD^ &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	test_cmp expected-unsigned actual
'

test_expect_success 'rebase --exec --signoff adds a sign-off line' '
	test_when_finished "rm exec" &&
	git commit --amend -m "first" &&
	git rebase --exec "touch exec" --signoff HEAD^ &&
	test_path_is_file exec &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'

test_expect_success 'rebase --root --signoff adds a sign-off line' '
	git commit --amend -m "first" &&
	git rebase --root --keep-empty --signoff &&
	git cat-file commit HEAD^ | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-initial-signed actual &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'

test_expect_success 'rebase -i --signoff fails' '
	git commit --amend -m "first" &&
	git rebase -i --signoff HEAD^ &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'

test_expect_success 'rebase -m --signoff fails' '
	git commit --amend -m "first" &&
	git rebase -m --signoff HEAD^ &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'
test_done
