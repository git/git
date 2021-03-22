#!/bin/sh
#
# Copyright (c) 2018 Phillip Wood
#

test_description='git rebase interactive fixup options

This test checks the "fixup [-C|-c]" command of rebase interactive.
In addition to amending the contents of the commit, "fixup -C"
replaces the original commit message with the message of the fixup
commit. "fixup -c" also replaces the original message, but opens the
editor to allow the user to edit the message before committing.
'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

EMPTY=""

test_commit_message () {
	rev="$1" && # commit or tag we want to test
	file="$2" && # test against the content of a file
	git show --no-patch --pretty=format:%B "$rev" >actual-message &&
	if test "$2" = -m
	then
		str="$3" && # test against a string
		printf "%s\n" "$str" >tmp-expected-message &&
		file="tmp-expected-message"
	fi
	test_cmp "$file" actual-message
}

get_author () {
	rev="$1" &&
	git log -1 --pretty=format:"%an %ae" "$rev"
}

test_expect_success 'setup' '
	cat >message <<-EOF &&
		amend! B
		${EMPTY}
		new subject
		${EMPTY}
		new
		body
		EOF

	sed "1,2d" message >expected-message &&

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
	test_tick &&
	git commit --allow-empty -F - <<-EOF &&
		amend! B
		${EMPTY}
		B
		${EMPTY}
		edited 1
		EOF
	test_tick &&
	git commit --allow-empty -F - <<-EOF &&
		amend! amend! B
		${EMPTY}
		B
		${EMPTY}
		edited 1
		${EMPTY}
		edited 2
		EOF
	echo B2 >B &&
	test_tick &&
	FAKE_COMMIT_AMEND="edited squash" git commit --squash=HEAD -a &&
	echo B3 >B &&
	test_tick &&
	git commit -a -F - <<-EOF &&
		amend! amend! amend! B
		${EMPTY}
		B
		${EMPTY}
		edited 1
		${EMPTY}
		edited 2
		${EMPTY}
		edited 3
		EOF

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
	test_must_fail env FAKE_LINES="1 fixup_-C 2" git rebase -i conflicts &&
	git checkout --theirs -- A &&
	git add A &&
	FAKE_COMMIT_AMEND=edited git rebase --continue &&
	test_cmp_rev HEAD^ conflicts &&
	test_cmp_rev HEAD^{tree} A1^{tree} &&
	test_write_lines "" edited >>expected-message &&
	test_commit_message HEAD expected-message &&
	get_author HEAD >actual-author &&
	test_cmp expected-author actual-author
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
	git checkout --detach branch &&
	FAKE_LINES="1 fixup 2 fixup_-C 3 fixup_-C 4 squash 5 fixup_-C 6" \
		FAKE_COMMIT_AMEND=squashed \
		FAKE_MESSAGE_COPY=actual-squash-message \
		git -c commit.status=false rebase -ik --signoff A &&
	git diff-tree --exit-code --patch HEAD branch -- &&
	test_cmp_rev HEAD^ A &&
	test_i18ncmp "$TEST_DIRECTORY/t3437/expected-squash-message" \
		actual-squash-message
'

test_expect_success 'first fixup -C commented out in sequence fixup fixup -C fixup -C' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout branch && git checkout --detach branch~2 &&
	git log -1 --pretty=format:%b >expected-message &&
	FAKE_LINES="1 fixup 2 fixup_-C 3 fixup_-C 4" git rebase -i A &&
	test_cmp_rev HEAD^ A &&
	test_commit_message HEAD expected-message
'

test_expect_success 'multiple fixup -c opens editor once' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A3 &&
	base=$(git rev-parse HEAD~4) &&
	FAKE_COMMIT_MESSAGE="Modified-A3" \
		FAKE_LINES="1 fixup_-C 2 fixup_-c 3 fixup_-c 4" \
		EXPECT_HEADER_COUNT=4 \
		git rebase -i $base &&
	test_cmp_rev $base HEAD^ &&
	test 1 = $(git show | grep Modified-A3 | wc -l)
'

test_expect_success 'sequence squash, fixup & fixup -c gives combined message' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout --detach A3 &&
	FAKE_LINES="1 squash 2 fixup 3 fixup_-c 4" \
		FAKE_MESSAGE_COPY=actual-combined-message \
		git -c commit.status=false rebase -i A &&
	test_i18ncmp "$TEST_DIRECTORY/t3437/expected-combined-message" \
		actual-combined-message &&
	test_cmp_rev HEAD^ A
'

test_expect_success 'fixup -C works upon --autosquash with amend!' '
	git checkout --detach branch &&
	FAKE_COMMIT_AMEND=squashed \
		FAKE_MESSAGE_COPY=actual-squash-message \
		git -c commit.status=false rebase -ik --autosquash \
						--signoff A &&
	git diff-tree --exit-code --patch HEAD branch -- &&
	test_cmp_rev HEAD^ A &&
	test_i18ncmp "$TEST_DIRECTORY/t3437/expected-squash-message" \
		actual-squash-message
'

test_done
