#!/bin/sh

test_description='switch basic functionality'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit first &&
	git branch first-branch &&
	test_commit second &&
	test_commit third &&
	git remote add origin nohost:/nopath &&
	git update-ref refs/remotes/origin/foo first-branch
'

test_expect_success 'switch branch no arguments' '
	test_must_fail git switch
'

test_expect_success 'switch branch' '
	git switch first-branch &&
	test_path_is_missing second.t
'

test_expect_success 'switch and detach' '
	test_when_finished git switch main &&
	test_must_fail git switch main^{commit} &&
	git switch --detach main^{commit} &&
	test_must_fail git symbolic-ref HEAD
'

test_expect_success 'suggestion to detach' '
	test_must_fail git switch main^{commit} 2>stderr &&
	grep "try again with the --detach option" stderr
'

test_expect_success 'suggestion to detach is suppressed with advice.suggestDetachingHead=false' '
	test_config advice.suggestDetachingHead false &&
	test_must_fail git switch main^{commit} 2>stderr &&
	! grep "try again with the --detach option" stderr
'

test_expect_success 'switch and detach current branch' '
	test_when_finished git switch main &&
	git switch main &&
	git switch --detach &&
	test_must_fail git symbolic-ref HEAD
'

test_expect_success 'switch and create branch' '
	test_when_finished git switch main &&
	git switch -c temp main^ &&
	test_cmp_rev main^ refs/heads/temp &&
	echo refs/heads/temp >expected-branch &&
	git symbolic-ref HEAD >actual-branch &&
	test_cmp expected-branch actual-branch
'

test_expect_success 'force create branch from HEAD' '
	test_when_finished git switch main &&
	git switch --detach main &&
	test_must_fail git switch -c temp &&
	git switch -C temp &&
	test_cmp_rev main refs/heads/temp &&
	echo refs/heads/temp >expected-branch &&
	git symbolic-ref HEAD >actual-branch &&
	test_cmp expected-branch actual-branch
'

test_expect_success 'new orphan branch from empty' '
	test_when_finished git switch main &&
	test_must_fail git switch --orphan new-orphan HEAD &&
	git switch --orphan new-orphan &&
	test_commit orphan &&
	git cat-file commit refs/heads/new-orphan >commit &&
	! grep ^parent commit &&
	git ls-files >tracked-files &&
	echo orphan.t >expected &&
	test_cmp expected tracked-files
'

test_expect_success 'orphan branch works with --discard-changes' '
	test_when_finished git switch main &&
	echo foo >foo.txt &&
	git switch --discard-changes --orphan new-orphan2 &&
	git ls-files >tracked-files &&
	test_must_be_empty tracked-files
'

test_expect_success 'switching ignores file of same branch name' '
	test_when_finished git switch main &&
	: >first-branch &&
	git switch first-branch &&
	echo refs/heads/first-branch >expected &&
	git symbolic-ref HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'guess and create branch' '
	test_when_finished git switch main &&
	test_must_fail git switch --no-guess foo &&
	test_config checkout.guess false &&
	test_must_fail git switch foo &&
	test_config checkout.guess true &&
	git switch foo &&
	echo refs/heads/foo >expected &&
	git symbolic-ref HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'not switching when something is in progress' '
	test_when_finished rm -f .git/MERGE_HEAD &&
	# fake a merge-in-progress
	cp .git/HEAD .git/MERGE_HEAD &&
	test_must_fail git switch -d @^
'

test_expect_success 'tracking info copied with autoSetupMerge=inherit' '
	# default config does not copy tracking info
	git switch -c foo-no-inherit foo &&
	test_cmp_config "" --default "" branch.foo-no-inherit.remote &&
	test_cmp_config "" --default "" branch.foo-no-inherit.merge &&
	# with --track=inherit, we copy tracking info from foo
	git switch --track=inherit -c foo2 foo &&
	test_cmp_config origin branch.foo2.remote &&
	test_cmp_config refs/heads/foo branch.foo2.merge &&
	# with autoSetupMerge=inherit, we do the same
	test_config branch.autoSetupMerge inherit &&
	git switch -c foo3 foo &&
	test_cmp_config origin branch.foo3.remote &&
	test_cmp_config refs/heads/foo branch.foo3.merge &&
	# with --track, we override autoSetupMerge
	git switch --track -c foo4 foo &&
	test_cmp_config . branch.foo4.remote &&
	test_cmp_config refs/heads/foo branch.foo4.merge &&
	# and --track=direct does as well
	git switch --track=direct -c foo5 foo &&
	test_cmp_config . branch.foo5.remote &&
	test_cmp_config refs/heads/foo branch.foo5.merge &&
	# no tracking info to inherit from main
	git switch -c main2 main &&
	test_cmp_config "" --default "" branch.main2.remote &&
	test_cmp_config "" --default "" branch.main2.merge
'

test_expect_success 'switch back when temporarily detached and checked out elsewhere ' '
	test_when_finished "
		git worktree remove wt1 ||:
		git worktree remove wt2 ||:
		git checkout - ||:
		git branch -D shared ||:
	" &&
	git checkout -b shared &&
	test_commit shared-first &&
	HASH1=$(git rev-parse --verify HEAD) &&
	test_commit shared-second &&
	test_commit shared-third &&
	HASH2=$(git rev-parse --verify HEAD) &&
	git worktree add wt1 -f shared &&
	git -C wt1 bisect start &&
	git -C wt1 bisect good $HASH1 &&
	git -C wt1 bisect bad $HASH2 &&
	git worktree add wt2 -f shared &&
	git -C wt2 bisect start &&
	git -C wt2 bisect good $HASH1 &&
	git -C wt2 bisect bad $HASH2 &&
	# we test in both worktrees to ensure that works
	# as expected with "first" and "next" worktrees
	test_must_fail git -C wt1 switch shared &&
	test_must_fail git -C wt1 switch -C shared &&
	git -C wt1 switch --ignore-other-worktrees shared &&
	test_must_fail git -C wt2 switch shared &&
	test_must_fail git -C wt2 switch -C shared &&
	git -C wt2 switch --ignore-other-worktrees shared
'

test_done
