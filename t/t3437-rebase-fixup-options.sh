#!/bin/sh
#
# Copyright (c) 2018 Phillip Wood
#

test_description='git rebase interactive fixup options

This test checks the "fixup [-C|-c]" command of rebase interactive.
In addition to amending the contents of the commit, "fixup -C"
replaces the original commit message with the message of the fixup
commit. "fixup -c" also replaces the original message, but opens the
editor to allow the user to edit the message before committing. Similar
to the "fixup" command that works with "fixup!", "fixup -C" works with
"amend!" upon --autosquash.
'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

EMPTY=""

get_author () {
	rev="$1" &&
	git log -1 --pretty=format:"%an %ae %at" "$rev"
}

test_expect_success 'setup' '
	cat >message <<-EOF &&
	amend! B
	$EMPTY
	new subject
	$EMPTY
	new
	body
	EOF

	test_commit initial &&
	test_commit A A &&
	test_commit B B &&
	get_author HEAD >expected-author &&
	ORIG_AUTHOR_NAME="$GIT_AUTHOR_NAME" &&
	ORIG_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" &&
	GIT_AUTHOR_NAME="Amend Author" &&
	GIT_AUTHOR_EMAIL="amend@example.com" &&
	test_commit "$(cat message)" A A1 A1 &&
	test_commit A2 A &&
	test_commit A3 A &&
	GIT_AUTHOR_NAME="$ORIG_AUTHOR_NAME" &&
	GIT_AUTHOR_EMAIL="$ORIG_AUTHOR_EMAIL" &&
	git checkout -b conflicts-branch A &&
	test_commit conflicts A &&

	set_fake_editor &&
	git checkout -b branch B &&
	echo B1 >B &&
	test_tick &&
	git commit --fixup=HEAD -a &&
	git tag B1 &&
	test_tick &&
	FAKE_COMMIT_AMEND="edited 1" git commit --fixup=reword:B &&
	test_tick &&
	FAKE_COMMIT_AMEND="edited 2" git commit --fixup=reword:HEAD &&
	echo B2 >B &&
	test_tick &&
	FAKE_COMMIT_AMEND="edited squash" git commit --squash=HEAD -a &&
	git tag B2 &&
	echo B3 >B &&
	test_tick &&
	FAKE_COMMIT_AMEND="edited 3" git commit -a --fixup=amend:HEAD^ &&
	git tag B3 &&

	GIT_AUTHOR_NAME="Rebase Author" &&
	GIT_AUTHOR_EMAIL="rebase.author@example.com" &&
	GIT_COMMITTER_NAME="Rebase Committer" &&
	GIT_COMMITTER_EMAIL="rebase.committer@example.com"
'

test_expect_success 'simple fixup -C works' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A2 &&
	FAKE_LINES="1 fixup_-C 2" git rebase -i B &&
	test_cmp_rev HEAD^ B &&
	test_cmp_rev HEAD^{tree} A2^{tree} &&
	test_commit_message HEAD -m "A2"
'

test_expect_success 'simple fixup -c works' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A2 &&
	git log -1 --pretty=format:%B >expected-fixup-message &&
	test_write_lines "" "Modified A2" >>expected-fixup-message &&
	FAKE_LINES="1 fixup_-c 2" \
		FAKE_COMMIT_AMEND="Modified A2" \
		git rebase -i B &&
	test_cmp_rev HEAD^ B &&
	test_cmp_rev HEAD^{tree} A2^{tree} &&
	test_commit_message HEAD expected-fixup-message
'

test_expect_success 'fixup -C removes amend! from message' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A1 &&
	git log -1 --pretty=format:%b >expected-message &&
	FAKE_LINES="1 fixup_-C 2" git rebase -i A &&
	test_cmp_rev HEAD^ A &&
	test_cmp_rev HEAD^{tree} A1^{tree} &&
	test_commit_message HEAD expected-message &&
	get_author HEAD >actual-author &&
	test_cmp expected-author actual-author
'

test_expect_success 'fixup -C with conflicts gives correct message' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A1 &&
	git log -1 --pretty=format:%b >expected-message &&
	test_write_lines "" "edited" >>expected-message &&
	test_must_fail env FAKE_LINES="1 fixup_-C 2" git rebase -i conflicts &&
	git checkout --theirs -- A &&
	git add A &&
	FAKE_COMMIT_AMEND=edited git rebase --continue &&
	test_cmp_rev HEAD^ conflicts &&
	test_cmp_rev HEAD^{tree} A1^{tree} &&
	test_commit_message HEAD expected-message &&
	get_author HEAD >actual-author &&
	test_cmp expected-author actual-author
'

test_expect_success 'conflicting fixup -C after fixup with custom comment string' '
	test_config core.commentString COMMENT &&
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A3 &&
	test_must_fail env FAKE_LINES="1 fixup 2 fixup_-C 4" git rebase -i A &&
	echo resolved >A &&
	git add A &&
	FAKE_COMMIT_AMEND=edited git rebase --continue &&
	test_commit_message HEAD <<-\EOF
	A3

	edited
	EOF
'

test_expect_success 'skipping fixup -C after fixup gives correct message' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A3 &&
	test_must_fail env FAKE_LINES="1 fixup 2 fixup_-C 4" git rebase -i A &&
	git reset --hard &&
	FAKE_COMMIT_AMEND=edited git rebase --continue &&
	test_commit_message HEAD -m "B"
'

test_expect_success 'sequence of fixup, fixup -C & squash --signoff works' '
	git checkout --detach B3 &&
	FAKE_LINES="1 fixup 2 fixup_-C 3 fixup_-C 4 squash 5 fixup_-C 6" \
		FAKE_COMMIT_AMEND=squashed \
		FAKE_MESSAGE_COPY=actual-squash-message \
		git -c commit.status=false rebase -ik --signoff A &&
	git diff-tree --exit-code --patch HEAD B3 -- &&
	test_cmp_rev HEAD^ A &&
	test_cmp "$TEST_DIRECTORY/t3437/expected-squash-message" \
		actual-squash-message
'

test_expect_success 'first fixup -C commented out in sequence fixup fixup -C fixup -C' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach B2~ &&
	git log -1 --pretty=format:%b >expected-message &&
	FAKE_LINES="1 fixup 2 fixup_-C 3 fixup_-C 4" git rebase -i A &&
	test_cmp_rev HEAD^ A &&
	test_commit_message HEAD expected-message
'

test_expect_success 'multiple fixup -c opens editor once' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A3 &&
	git log -1 --pretty=format:%B >expected-message &&
	test_write_lines "" "Modified-A3" >>expected-message &&
	FAKE_COMMIT_AMEND="Modified-A3" \
		FAKE_LINES="1 fixup_-C 2 fixup_-c 3 fixup_-c 4" \
		EXPECT_HEADER_COUNT=4 \
		git rebase -i A &&
	test_cmp_rev HEAD^ A &&
	get_author HEAD >actual-author &&
	test_cmp expected-author actual-author &&
	test_commit_message HEAD expected-message
'

test_expect_success 'sequence squash, fixup & fixup -c gives combined message' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A3 &&
	FAKE_LINES="1 squash 2 fixup 3 fixup_-c 4" \
		FAKE_MESSAGE_COPY=actual-combined-message \
		git -c commit.status=false rebase -i A &&
	test_cmp "$TEST_DIRECTORY/t3437/expected-combined-message" \
		actual-combined-message &&
	test_cmp_rev HEAD^ A
'

test_expect_success 'fixup -C works upon --autosquash with amend!' '
	git checkout --detach B3 &&
	FAKE_COMMIT_AMEND=squashed \
		FAKE_MESSAGE_COPY=actual-squash-message \
		git -c commit.status=false rebase -ik --autosquash \
						--signoff A &&
	git diff-tree --exit-code --patch HEAD B3 -- &&
	test_cmp_rev HEAD^ A &&
	test_cmp "$TEST_DIRECTORY/t3437/expected-squash-message" \
		actual-squash-message
'

test_expect_success 'fixup -[Cc]<commit> works' '
	test_when_finished "test_might_fail git rebase --abort" &&
	cat >todo <<-\EOF &&
	pick A
	fixup -CA1
	pick B
	fixup -cA2
	EOF
	(
		set_replace_editor todo &&
		FAKE_COMMIT_MESSAGE="edited and fixed up" \
			git rebase -i initial initial
	) &&
	git log --pretty=format:%B initial.. >actual &&
	cat >expect <<-EOF &&
	edited and fixed up
	$EMPTY
	new subject
	$EMPTY
	new
	body
	EOF
	test_cmp expect actual
'

test_done
