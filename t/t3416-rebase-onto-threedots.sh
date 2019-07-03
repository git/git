#!/bin/sh

test_description='git rebase --onto A...B'

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-rebase.sh"

# Rebase only the tip commit of "topic" on merge base between "master"
# and "topic".  Cannot do this for "side" with "master" because there
# is no single merge base.
#
#
#	    F---G topic                             G'
#	   /                                       /
# A---B---C---D---E master      -->       A---B---C---D---E
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
	git checkout master &&
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

test_expect_success 'rebase --onto master...topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	git rebase --onto master...topic F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --onto master...' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	git rebase --onto master... F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --onto master...side' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	test_must_fail git rebase --onto master...side J
'

test_expect_success 'rebase -i --onto master...topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&
	set_fake_editor &&
	EXPECT_COUNT=1 git rebase -i --onto master...topic F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i --onto master...' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&
	set_fake_editor &&
	EXPECT_COUNT=1 git rebase -i --onto master... F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i --onto master...side' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	set_fake_editor &&
	test_must_fail git rebase -i --onto master...side J
'

test_expect_success 'rebase --keep-base --onto incompatible' '
	test_must_fail git rebase --keep-base --onto master...
'

test_expect_success 'rebase --keep-base --root incompatible' '
	test_must_fail git rebase --keep-base --root
'

test_expect_success 'rebase --keep-base master from topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	git rebase --keep-base master &&
	git rev-parse C >base.expect &&
	git merge-base master HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	git rev-parse HEAD~2 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --keep-base master from side' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	test_must_fail git rebase --keep-base master
'

test_expect_success 'rebase -i --keep-base master from topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	set_fake_editor &&
	EXPECT_COUNT=2 git rebase -i --keep-base master &&
	git rev-parse C >base.expect &&
	git merge-base master HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	git rev-parse HEAD~2 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i --keep-base master from side' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	set_fake_editor &&
	test_must_fail git rebase -i --keep-base master
'

test_done
