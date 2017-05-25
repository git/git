#!/bin/sh

test_description='git rabassa --onto A...B'

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-rabassa.sh"

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

test_expect_success 'rabassa --onto master...topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	git rabassa --onto master...topic F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rabassa --onto master...' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&

	git rabassa --onto master... F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rabassa --onto master...side' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	test_must_fail git rabassa --onto master...side J
'

test_expect_success 'rabassa -i --onto master...topic' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&
	set_fake_editor &&
	EXPECT_COUNT=1 git rabassa -i --onto master...topic F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rabassa -i --onto master...' '
	git reset --hard &&
	git checkout topic &&
	git reset --hard G &&
	set_fake_editor &&
	EXPECT_COUNT=1 git rabassa -i --onto master... F &&
	git rev-parse HEAD^1 >actual &&
	git rev-parse C^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'rabassa -i --onto master...side' '
	git reset --hard &&
	git checkout side &&
	git reset --hard K &&

	test_must_fail git rabassa -i --onto master...side J
'

test_done
