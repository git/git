#!/bin/sh

test_description='git rebase --signoff

This test runs git rebase --signoff and make sure that it works.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup' '
	git commit --allow-empty -m "Initial empty commit" &&
	test_commit first file a &&
	test_commit second file &&
	git checkout -b conflict-branch first &&
	test_commit file-2 file-2 &&
	test_commit conflict file &&
	test_commit third file &&

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

	# Expected commit message after conflict resolution for rebase --signoff
	cat >expected-signed-conflict <<-EOF &&
	third

	Signed-off-by: $ident

	conflict

	Signed-off-by: $ident

	file-2

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
	test_must_fail git rbs --apply second third &&
	git checkout --theirs file &&
	git add file &&
	git rebase --continue &&
	git log --format=%B -n3 >actual &&
	test_cmp expected-signed-conflict actual
'

test_expect_success 'rebase --no-signoff does not add a sign-off line' '
	git commit --amend -m "first" &&
	git rbs --no-signoff HEAD^ &&
	test_commit_message HEAD expected-unsigned
'

test_expect_success 'rebase --exec --signoff adds a sign-off line' '
	test_when_finished "rm exec" &&
	git rebase --exec "touch exec" --signoff first^ first &&
	test_path_is_file exec &&
	test_commit_message HEAD expected-signed
'

test_expect_success 'rebase --root --signoff adds a sign-off line' '
	git checkout first &&
	git rebase --root --keep-empty --signoff &&
	test_commit_message HEAD^ expected-initial-signed &&
	test_commit_message HEAD expected-signed
'

test_expect_success 'rebase -m --signoff adds a sign-off line' '
	test_must_fail git rebase -m --signoff second third &&
	git checkout --theirs file &&
	git add file &&
	GIT_EDITOR="sed -n /Conflicts:/,/^\\\$/p >actual" \
		git rebase --continue &&
	cat >expect <<-\EOF &&
	# Conflicts:
	#	file

	EOF
	test_cmp expect actual &&
	git log --format=%B -n3 >actual &&
	test_cmp expected-signed-conflict actual
'

test_expect_success 'rebase -i --signoff adds a sign-off line when editing commit' '
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 edit 3 edit 2" \
			git rebase -i --signoff first third
	) &&
	echo a >a &&
	git add a &&
	test_must_fail git rebase --continue &&
	git checkout --ours file &&
	echo b >a &&
	git add a file &&
	git rebase --continue &&
	echo c >a &&
	git add a &&
	git log --format=%B -n3 >actual &&
	cat >expect <<-EOF &&
	conflict

	Signed-off-by: $ident

	third

	Signed-off-by: $ident

	file-2

	Signed-off-by: $ident

	EOF
	test_cmp expect actual
'

test_done
