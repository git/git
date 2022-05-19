#!/bin/sh

test_description='but rebase --onto A...B'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-rebase.sh"

# Rebase only the tip cummit of "topic" on merge base between "main"
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
	test_cummit A &&
	test_cummit B &&
	but branch side &&
	test_cummit C &&
	but branch topic &&
	but checkout side &&
	test_commit H &&
	but checkout main &&
	test_tick &&
	but merge H &&
	but tag D &&
	test_cummit E &&
	but checkout topic &&
	test_cummit F &&
	test_cummit G &&
	but checkout side &&
	test_tick &&
	but merge C &&
	but tag I &&
	test_cummit J &&
	test_cummit K
'

test_expect_success 'rebase --onto main...topic' '
	but reset --hard &&
	but checkout topic &&
	but reset --hard G &&

	but rebase --onto main...topic F &&
	but rev-parse HEAD^1 >actual &&
	but rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --onto main...' '
	but reset --hard &&
	but checkout topic &&
	but reset --hard G &&

	but rebase --onto main... F &&
	but rev-parse HEAD^1 >actual &&
	but rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --onto main...side' '
	but reset --hard &&
	but checkout side &&
	but reset --hard K &&

	test_must_fail but rebase --onto main...side J
'

test_expect_success 'rebase -i --onto main...topic' '
	but reset --hard &&
	but checkout topic &&
	but reset --hard G &&
	set_fake_editor &&
	EXPECT_COUNT=1 but rebase -i --onto main...topic F &&
	but rev-parse HEAD^1 >actual &&
	but rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i --onto main...' '
	but reset --hard &&
	but checkout topic &&
	but reset --hard G &&
	set_fake_editor &&
	EXPECT_COUNT=1 but rebase -i --onto main... F &&
	but rev-parse HEAD^1 >actual &&
	but rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i --onto main...side' '
	but reset --hard &&
	but checkout side &&
	but reset --hard K &&

	set_fake_editor &&
	test_must_fail but rebase -i --onto main...side J
'

test_expect_success 'rebase --keep-base --onto incompatible' '
	test_must_fail but rebase --keep-base --onto main...
'

test_expect_success 'rebase --keep-base --root incompatible' '
	test_must_fail but rebase --keep-base --root
'

test_expect_success 'rebase --keep-base main from topic' '
	but reset --hard &&
	but checkout topic &&
	but reset --hard G &&

	but rebase --keep-base main &&
	but rev-parse C >base.expect &&
	but merge-base main HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	but rev-parse HEAD~2 >actual &&
	but rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --keep-base main topic from main' '
	but checkout main &&
	but branch -f topic G &&

	but rebase --keep-base main topic &&
	but rev-parse C >base.expect &&
	but merge-base main HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	but rev-parse HEAD~2 >actual &&
	but rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase --keep-base main from side' '
	but reset --hard &&
	but checkout side &&
	but reset --hard K &&

	test_must_fail but rebase --keep-base main
'

test_expect_success 'rebase -i --keep-base main from topic' '
	but reset --hard &&
	but checkout topic &&
	but reset --hard G &&

	set_fake_editor &&
	EXPECT_COUNT=2 but rebase -i --keep-base main &&
	but rev-parse C >base.expect &&
	but merge-base main HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	but rev-parse HEAD~2 >actual &&
	but rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i --keep-base main topic from main' '
	but checkout main &&
	but branch -f topic G &&

	set_fake_editor &&
	EXPECT_COUNT=2 but rebase -i --keep-base main topic &&
	but rev-parse C >base.expect &&
	but merge-base main HEAD >base.actual &&
	test_cmp base.expect base.actual &&

	but rev-parse HEAD~2 >actual &&
	but rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i --keep-base main from side' '
	but reset --hard &&
	but checkout side &&
	but reset --hard K &&

	set_fake_editor &&
	test_must_fail but rebase -i --keep-base main
'

test_done
