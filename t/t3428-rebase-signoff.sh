#!/bin/sh

test_description='git rebase --signoff

This test runs git rebase --signoff and make sure that it works.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	git commit --allow-empty -m "Initial empty commit" &&
	test_commit first file a &&

	ident="$GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" &&

	# Expected commit message for initial commit after rebase --signoff
	cat >expected-initial-signed <<-EOF &&
	Initial empty commit

	Signed-off-by: $ident
	EOF

	# Expected commit message after rebase --signoff
	cat >expected-signed <<-EOF &&
	first

	Signed-off-by: $ident
	EOF

	# Expected commit message after rebase without --signoff (or with --no-signoff)
	cat >expected-unsigned <<-EOF &&
	first
	EOF

	git config alias.rbs "rebase --signoff"
'

# We configure an alias to do the rebase --signoff so that
# on the next subtest we can show that --no-signoff overrides the alias
test_expect_success 'rebase --apply --signoff adds a sign-off line' '
	git rbs --apply HEAD^ &&
	test_commit_message HEAD expected-signed
'

test_expect_success 'rebase --no-signoff does not add a sign-off line' '
	git commit --amend -m "first" &&
	git rbs --no-signoff HEAD^ &&
	test_commit_message HEAD expected-unsigned
'

test_expect_success 'rebase --exec --signoff adds a sign-off line' '
	test_when_finished "rm exec" &&
	git commit --amend -m "first" &&
	git rebase --exec "touch exec" --signoff HEAD^ &&
	test_path_is_file exec &&
	test_commit_message HEAD expected-signed
'

test_expect_success 'rebase --root --signoff adds a sign-off line' '
	git commit --amend -m "first" &&
	git rebase --root --keep-empty --signoff &&
	test_commit_message HEAD^ expected-initial-signed &&
	test_commit_message HEAD expected-signed
'

test_expect_success 'rebase -i --signoff fails' '
	git commit --amend -m "first" &&
	git rebase -i --signoff HEAD^ &&
	test_commit_message HEAD expected-signed
'

test_expect_success 'rebase -m --signoff fails' '
	git commit --amend -m "first" &&
	git rebase -m --signoff HEAD^ &&
	test_commit_message HEAD expected-signed
'
test_done
