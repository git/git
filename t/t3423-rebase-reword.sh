#!/bin/sh

test_description='git rebase interactive with rewording'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup' '
	test_commit master file-1 test &&

	git checkout -b stuff &&

	test_commit feature_a file-2 aaa &&
	test_commit feature_b file-2 ddd
'

test_expect_success 'reword without issues functions as intended' '
	test_when_finished "reset_rebase" &&

	git checkout stuff^0 &&

	set_fake_editor &&
	FAKE_LINES="pick 1 reword 2" FAKE_COMMIT_MESSAGE="feature_b_reworded" \
		git rebase -i -v master &&

	test "$(git log -1 --format=%B)" = "feature_b_reworded" &&
	test $(git rev-list --count HEAD) = 3
'

test_expect_success 'reword after a conflict preserves commit' '
	test_when_finished "reset_rebase" &&

	git checkout stuff^0 &&

	set_fake_editor &&
	test_must_fail env FAKE_LINES="reword 2" \
		git rebase -i -v master &&

	git checkout --theirs file-2 &&
	git add file-2 &&
	FAKE_COMMIT_MESSAGE="feature_b_reworded" git rebase --continue &&

	test "$(git log -1 --format=%B)" = "feature_b_reworded" &&
	test $(git rev-list --count HEAD) = 2
'

test_done
