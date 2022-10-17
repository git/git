#!/bin/sh

test_description='git rebase --onto A...B'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-rebase.sh"

# Rebase only the tip commit of "topic" on merge base between "main"
# and "topic".  Cannot do this for "side" with "main" because there
# is no single merge base.
#
#
#	    F---G topic                             G'
#	   /                                       /
# A---B---C---D---E main        -->       A---B---C---D---E
#      \   \ /
#	\   x
#	 \ / \
#	  H---I---J---K side

test_expect_success setup '
	test_commit A &&
	test_commit B &&
	git branch side &&
	test_commit C &&
	git branch topic &&
	git checkout side &&
	test_commit H &&
	git checkout main &&
	test_tick &&
	git merge H &&
	git tag D &&
	test_commit E &&
	git checkout topic &&
	test_commit F &&
	test_commit G &&
	git checkout side &&
	test_tick &&
	git merge C &&
	git tag I &&
	test_commit J &&
	test_commit K
'

test_expect_success 'rebase --onto main...topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	git rebase --onto main...topic F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --onto main...' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	git rebase --onto main... F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --onto main...side' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	test_must_fail git rebase --onto main...side J
'

test_expect_success 'rebase -i --onto main...topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&
	(
		set_fake_editor &&
		EXPECT_COUNT=1 git rebase -i --onto main...topic F
	) &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i --onto main...' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&
	(
		set_fake_editor &&
		EXPECT_COUNT=1 git rebase -i --onto main... F
	) &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --onto main...side requires a single merge-base' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	test_must_fail git rebase -i --onto main...side J 2>err &&
	grep "need exactly one merge base" err
'

test_expect_success 'rebase --keep-base --onto incompatible' '
	test_must_fail git rebase --keep-base --onto main...
'

test_expect_success 'rebase --keep-base --root incompatible' '
	test_must_fail git rebase --keep-base --root
'

test_expect_success 'rebase --keep-base main from topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	git rebase --keep-base main &&
	git rev-parse C >base.expect &&
	git merge-base main HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	git rev-parse HEAD~2 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --keep-base main topic from main' '
	git checkout main &&
	git branch -f topic G &&

	git rebase --keep-base main topic &&
	git rev-parse C >base.expect &&
	git merge-base main HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	git rev-parse HEAD~2 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --keep-base main from side' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	test_must_fail git rebase --keep-base main
'

test_expect_success 'rebase -i --keep-base main from topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	(
		set_fake_editor &&
		EXPECT_COUNT=2 git rebase -i --keep-base main
	) &&
	git rev-parse C >base.expect &&
	git merge-base main HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	git rev-parse HEAD~2 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i --keep-base main topic from main' '
	git checkout main &&
	git branch -f topic G &&

	(
		set_fake_editor &&
		EXPECT_COUNT=2 git rebase -i --keep-base main topic
	) &&
	git rev-parse C >base.expect &&
	git merge-base main HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	git rev-parse HEAD~2 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --keep-base requires a single merge base' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	test_must_fail git rebase -i --keep-base main 2>err &&
	grep "need exactly one merge base with branch" err
'

test_expect_success 'rebase --keep-base keeps cherry picks' '
	git checkout -f -B main E &&
	git cherry-pick F &&
	(
		set_fake_editor &&
		EXPECT_COUNT=2 git rebase -i --keep-base HEAD G
	) &&
	test_cmp_rev HEAD G
'

test_expect_success 'rebase --keep-base --no-reapply-cherry-picks' '
	git checkout -f -B main E &&
	git cherry-pick F &&
	(
		set_fake_editor &&
		EXPECT_COUNT=1 git rebase -i --keep-base \
					--no-reapply-cherry-picks HEAD G
	) &&
	test_cmp_rev HEAD^ C
'

# This must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
