#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git rebase --merge --skip tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

# we assume the default git am -3 --skip strategy is tested independently
# and always works :)

test_expect_success setup '
	echo hello > hello &&
	git add hello &&
	git commit -m "hello" &&
	git branch skip-reference &&
	git tag hello &&

	echo world >> hello &&
	git commit -a -m "hello world" &&
	echo goodbye >> hello &&
	git commit -a -m "goodbye" &&
	git tag goodbye &&

	git checkout --detach &&
	git checkout HEAD^ . &&
	test_tick &&
	git commit -m reverted-goodbye &&
	git tag reverted-goodbye &&
	git checkout goodbye &&
	test_tick &&
	GIT_AUTHOR_NAME="Another Author" \
		GIT_AUTHOR_EMAIL="another.author@example.com" \
		git commit --amend --no-edit -m amended-goodbye \
			--reset-author &&
	test_tick &&
	git tag amended-goodbye &&

	git checkout -f skip-reference &&
	echo moo > hello &&
	git commit -a -m "we should skip this" &&
	echo moo > cow &&
	git add cow &&
	git commit -m "this should not be skipped" &&
	git branch pre-rebase skip-reference &&
	git branch skip-merge skip-reference
	'

test_expect_success 'rebase with git am -3 (default)' '
	test_must_fail git rebase --apply main
'

test_expect_success 'rebase --skip can not be used with other options' '
	test_must_fail git rebase -v --skip &&
	test_must_fail git rebase --skip -v
'

test_expect_success 'rebase --skip with am -3' '
	git rebase --skip
	'

test_expect_success 'rebase moves back to skip-reference' '
	test refs/heads/skip-reference = $(git symbolic-ref HEAD) &&
	git branch post-rebase &&
	git reset --hard pre-rebase &&
	test_must_fail git rebase main &&
	echo "hello" > hello &&
	git add hello &&
	git rebase --continue &&
	test refs/heads/skip-reference = $(git symbolic-ref HEAD) &&
	git reset --hard post-rebase
'

test_expect_success 'checkout skip-merge' 'git checkout -f skip-merge'

test_expect_success 'rebase with --merge' '
	test_must_fail git rebase --merge main
'

test_expect_success 'rebase --skip with --merge' '
	git rebase --skip
'

test_expect_success 'merge and reference trees equal' '
	test -z "$(git diff-tree skip-merge skip-reference)"
'

test_expect_success 'moved back to branch correctly' '
	test refs/heads/skip-merge = $(git symbolic-ref HEAD)
'

test_debug 'gitk --all & sleep 1'

test_expect_success 'skipping final pick removes .git/MERGE_MSG' '
	test_must_fail git rebase --onto hello reverted-goodbye^ \
		reverted-goodbye &&
	git rebase --skip &&
	test_path_is_missing .git/MERGE_MSG
'

test_expect_success 'correct advice upon picking empty commit' '
	test_when_finished "git rebase --abort" &&
	test_must_fail git rebase -i --onto goodbye \
		amended-goodbye^ amended-goodbye 2>err &&
	test_grep "previous cherry-pick is now empty" err &&
	test_grep "git rebase --skip" err &&
	test_must_fail git commit &&
	test_grep "git rebase --skip" err
'

test_expect_success 'correct authorship when committing empty pick' '
	test_when_finished "git rebase --abort" &&
	test_must_fail git rebase -i --onto goodbye \
		amended-goodbye^ amended-goodbye &&
	git commit --allow-empty &&
	git log --pretty=format:"%an <%ae>%n%ad%B" -1 amended-goodbye >expect &&
	git log --pretty=format:"%an <%ae>%n%ad%B" -1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'correct advice upon rewording empty commit' '
	test_when_finished "git rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="reword 1" git rebase -i \
			--onto goodbye amended-goodbye^ amended-goodbye 2>err
	) &&
	test_grep "previous cherry-pick is now empty" err &&
	test_grep "git rebase --skip" err &&
	test_must_fail git commit &&
	test_grep "git rebase --skip" err
'

test_expect_success 'correct advice upon editing empty commit' '
	test_when_finished "git rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="edit 1" git rebase -i \
			--onto goodbye amended-goodbye^ amended-goodbye 2>err
	) &&
	test_grep "previous cherry-pick is now empty" err &&
	test_grep "git rebase --skip" err &&
	test_must_fail git commit &&
	test_grep "git rebase --skip" err
'

test_expect_success 'correct advice upon cherry-picking an empty commit during a rebase' '
	test_when_finished "git rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 exec_git_cherry-pick_amended-goodbye" \
			git rebase -i goodbye^ goodbye 2>err
	) &&
	test_grep "previous cherry-pick is now empty" err &&
	test_grep "git cherry-pick --skip" err &&
	test_must_fail git commit 2>err &&
	test_grep "git cherry-pick --skip" err
'

test_expect_success 'correct advice upon multi cherry-pick picking an empty commit during a rebase' '
	test_when_finished "git rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 exec_git_cherry-pick_goodbye_amended-goodbye" \
			git rebase -i goodbye^^ goodbye 2>err
	) &&
	test_grep "previous cherry-pick is now empty" err &&
	test_grep "git cherry-pick --skip" err &&
	test_must_fail git commit 2>err &&
	test_grep "git cherry-pick --skip" err
'

test_expect_success 'fixup that empties commit fails' '
	test_when_finished "git rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 fixup 2" git rebase -i \
			goodbye^ reverted-goodbye
	)
'

test_expect_success 'squash that empties commit fails' '
	test_when_finished "git rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 squash 2" git rebase -i \
			goodbye^ reverted-goodbye
	)
'

# Must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
