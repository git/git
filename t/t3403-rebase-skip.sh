#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='but rebase --merge --skip tests'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

# we assume the default but am -3 --skip strategy is tested independently
# and always works :)

test_expect_success setup '
	echo hello > hello &&
	but add hello &&
	but cummit -m "hello" &&
	but branch skip-reference &&
	but tag hello &&

	echo world >> hello &&
	but cummit -a -m "hello world" &&
	echo goodbye >> hello &&
	but cummit -a -m "goodbye" &&
	but tag goodbye &&

	but checkout --detach &&
	but checkout HEAD^ . &&
	test_tick &&
	but cummit -m reverted-goodbye &&
	but tag reverted-goodbye &&
	but checkout goodbye &&
	test_tick &&
	BUT_AUTHOR_NAME="Another Author" \
		BUT_AUTHOR_EMAIL="another.author@example.com" \
		but cummit --amend --no-edit -m amended-goodbye \
			--reset-author &&
	test_tick &&
	but tag amended-goodbye &&

	but checkout -f skip-reference &&
	echo moo > hello &&
	but cummit -a -m "we should skip this" &&
	echo moo > cow &&
	but add cow &&
	but cummit -m "this should not be skipped" &&
	but branch pre-rebase skip-reference &&
	but branch skip-merge skip-reference
	'

test_expect_success 'rebase with but am -3 (default)' '
	test_must_fail but rebase --apply main
'

test_expect_success 'rebase --skip can not be used with other options' '
	test_must_fail but rebase -v --skip &&
	test_must_fail but rebase --skip -v
'

test_expect_success 'rebase --skip with am -3' '
	but rebase --skip
	'

test_expect_success 'rebase moves back to skip-reference' '
	test refs/heads/skip-reference = $(but symbolic-ref HEAD) &&
	but branch post-rebase &&
	but reset --hard pre-rebase &&
	test_must_fail but rebase main &&
	echo "hello" > hello &&
	but add hello &&
	but rebase --continue &&
	test refs/heads/skip-reference = $(but symbolic-ref HEAD) &&
	but reset --hard post-rebase
'

test_expect_success 'checkout skip-merge' 'but checkout -f skip-merge'

test_expect_success 'rebase with --merge' '
	test_must_fail but rebase --merge main
'

test_expect_success 'rebase --skip with --merge' '
	but rebase --skip
'

test_expect_success 'merge and reference trees equal' '
	test -z "$(but diff-tree skip-merge skip-reference)"
'

test_expect_success 'moved back to branch correctly' '
	test refs/heads/skip-merge = $(but symbolic-ref HEAD)
'

test_debug 'butk --all & sleep 1'

test_expect_success 'skipping final pick removes .but/MERGE_MSG' '
	test_must_fail but rebase --onto hello reverted-goodbye^ \
		reverted-goodbye &&
	but rebase --skip &&
	test_path_is_missing .but/MERGE_MSG
'

test_expect_success 'correct advice upon picking empty cummit' '
	test_when_finished "but rebase --abort" &&
	test_must_fail but rebase -i --onto goodbye \
		amended-goodbye^ amended-goodbye 2>err &&
	test_i18ngrep "previous cherry-pick is now empty" err &&
	test_i18ngrep "but rebase --skip" err &&
	test_must_fail but cummit &&
	test_i18ngrep "but rebase --skip" err
'

test_expect_success 'correct authorship when cummitting empty pick' '
	test_when_finished "but rebase --abort" &&
	test_must_fail but rebase -i --onto goodbye \
		amended-goodbye^ amended-goodbye &&
	but cummit --allow-empty &&
	but log --pretty=format:"%an <%ae>%n%ad%B" -1 amended-goodbye >expect &&
	but log --pretty=format:"%an <%ae>%n%ad%B" -1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'correct advice upon rewording empty cummit' '
	test_when_finished "but rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="reword 1" but rebase -i \
			--onto goodbye amended-goodbye^ amended-goodbye 2>err
	) &&
	test_i18ngrep "previous cherry-pick is now empty" err &&
	test_i18ngrep "but rebase --skip" err &&
	test_must_fail but cummit &&
	test_i18ngrep "but rebase --skip" err
'

test_expect_success 'correct advice upon editing empty cummit' '
	test_when_finished "but rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="edit 1" but rebase -i \
			--onto goodbye amended-goodbye^ amended-goodbye 2>err
	) &&
	test_i18ngrep "previous cherry-pick is now empty" err &&
	test_i18ngrep "but rebase --skip" err &&
	test_must_fail but cummit &&
	test_i18ngrep "but rebase --skip" err
'

test_expect_success 'correct advice upon cherry-picking an empty cummit during a rebase' '
	test_when_finished "but rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 exec_but_cherry-pick_amended-goodbye" \
			but rebase -i goodbye^ goodbye 2>err
	) &&
	test_i18ngrep "previous cherry-pick is now empty" err &&
	test_i18ngrep "but cherry-pick --skip" err &&
	test_must_fail but cummit 2>err &&
	test_i18ngrep "but cherry-pick --skip" err
'

test_expect_success 'correct advice upon multi cherry-pick picking an empty cummit during a rebase' '
	test_when_finished "but rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 exec_but_cherry-pick_goodbye_amended-goodbye" \
			but rebase -i goodbye^^ goodbye 2>err
	) &&
	test_i18ngrep "previous cherry-pick is now empty" err &&
	test_i18ngrep "but cherry-pick --skip" err &&
	test_must_fail but cummit 2>err &&
	test_i18ngrep "but cherry-pick --skip" err
'

test_expect_success 'fixup that empties cummit fails' '
	test_when_finished "but rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 fixup 2" but rebase -i \
			goodbye^ reverted-goodbye
	)
'

test_expect_success 'squash that empties cummit fails' '
	test_when_finished "but rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 squash 2" but rebase -i \
			goodbye^ reverted-goodbye
	)
'

# Must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
