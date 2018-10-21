#!/bin/sh

test_description='per-worktree refs'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial &&
	test_commit wt1 &&
	test_commit wt2 &&
	git worktree add wt1 wt1 &&
	git worktree add wt2 wt2 &&
	git checkout initial &&
	git update-ref refs/worktree/foo HEAD &&
	git -C wt1 update-ref refs/worktree/foo HEAD &&
	git -C wt2 update-ref refs/worktree/foo HEAD
'

test_expect_success 'refs/worktree must not be packed' '
	git pack-refs --all &&
	test_path_is_missing .git/refs/tags/wt1 &&
	test_path_is_file .git/refs/worktree/foo &&
	test_path_is_file .git/worktrees/wt1/refs/worktree/foo &&
	test_path_is_file .git/worktrees/wt2/refs/worktree/foo
'

test_expect_success 'refs/worktree are per-worktree' '
	test_cmp_rev worktree/foo initial &&
	( cd wt1 && test_cmp_rev worktree/foo wt1 ) &&
	( cd wt2 && test_cmp_rev worktree/foo wt2 )
'

test_expect_success 'resolve main-worktree/HEAD' '
	test_cmp_rev main-worktree/HEAD initial &&
	( cd wt1 && test_cmp_rev main-worktree/HEAD initial ) &&
	( cd wt2 && test_cmp_rev main-worktree/HEAD initial )
'

test_expect_success 'ambiguous main-worktree/HEAD' '
	mkdir -p .git/refs/heads/main-worktree &&
	test_when_finished rm -f .git/refs/heads/main-worktree/HEAD &&
	cp .git/HEAD .git/refs/heads/main-worktree/HEAD &&
	git rev-parse main-worktree/HEAD 2>warn &&
	grep "main-worktree/HEAD.*ambiguous" warn
'

test_expect_success 'resolve worktrees/xx/HEAD' '
	test_cmp_rev worktrees/wt1/HEAD wt1 &&
	( cd wt1 && test_cmp_rev worktrees/wt1/HEAD wt1 ) &&
	( cd wt2 && test_cmp_rev worktrees/wt1/HEAD wt1 )
'

test_expect_success 'ambiguous worktrees/xx/HEAD' '
	mkdir -p .git/refs/heads/worktrees/wt1 &&
	test_when_finished rm -f .git/refs/heads/worktrees/wt1/HEAD &&
	cp .git/HEAD .git/refs/heads/worktrees/wt1/HEAD &&
	git rev-parse worktrees/wt1/HEAD 2>warn &&
	grep "worktrees/wt1/HEAD.*ambiguous" warn
'

test_expect_success 'reflog of main-worktree/HEAD' '
	git reflog HEAD | sed "s/HEAD/main-worktree\/HEAD/" >expected &&
	git reflog main-worktree/HEAD >actual &&
	test_cmp expected actual &&
	git -C wt1 reflog main-worktree/HEAD >actual.wt1 &&
	test_cmp expected actual.wt1
'

test_expect_success 'reflog of worktrees/xx/HEAD' '
	git -C wt2 reflog HEAD | sed "s/HEAD/worktrees\/wt2\/HEAD/" >expected &&
	git reflog worktrees/wt2/HEAD >actual &&
	test_cmp expected actual &&
	git -C wt1 reflog worktrees/wt2/HEAD >actual.wt1 &&
	test_cmp expected actual.wt1 &&
	git -C wt2 reflog worktrees/wt2/HEAD >actual.wt2 &&
	test_cmp expected actual.wt2
'

test_done
