#!/bin/sh

test_description='per-worktree refs'

TEST_PASSES_SANITIZE_LEAK=true
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

# The 'packed-refs' file is stored directly in .git/. This means it is global
# to the repository, and can only contain refs that are shared across all
# worktrees.
test_expect_success REFFILES 'refs/worktree must not be packed' '
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
	test_when_finished git update-ref -d refs/heads/main-worktree/HEAD &&
	git update-ref refs/heads/main-worktree/HEAD $(git rev-parse HEAD) &&
	git rev-parse main-worktree/HEAD 2>warn &&
	grep "main-worktree/HEAD.*ambiguous" warn
'

test_expect_success 'resolve worktrees/xx/HEAD' '
	test_cmp_rev worktrees/wt1/HEAD wt1 &&
	( cd wt1 && test_cmp_rev worktrees/wt1/HEAD wt1 ) &&
	( cd wt2 && test_cmp_rev worktrees/wt1/HEAD wt1 )
'

test_expect_success 'ambiguous worktrees/xx/HEAD' '
	git update-ref refs/heads/worktrees/wt1/HEAD $(git rev-parse HEAD) &&
	test_when_finished git update-ref -d refs/heads/worktrees/wt1/HEAD &&
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

test_expect_success 'for-each-ref from main worktree' '
	mkdir fer1 &&
	git -C fer1 init repo &&
	test_commit -C fer1/repo initial &&
	git -C fer1/repo worktree add ../second &&
	git -C fer1/repo update-ref refs/bisect/first HEAD &&
	git -C fer1/repo update-ref refs/rewritten/first HEAD &&
	git -C fer1/repo update-ref refs/worktree/first HEAD &&
	git -C fer1/repo for-each-ref --format="%(refname)" | grep first >actual &&
	cat >expected <<-\EOF &&
	refs/bisect/first
	refs/rewritten/first
	refs/worktree/first
	EOF
	test_cmp expected actual
'

test_expect_success 'for-each-ref from linked worktree' '
	mkdir fer2 &&
	git -C fer2 init repo &&
	test_commit -C fer2/repo initial &&
	git -C fer2/repo worktree add ../second &&
	git -C fer2/second update-ref refs/bisect/second HEAD &&
	git -C fer2/second update-ref refs/rewritten/second HEAD &&
	git -C fer2/second update-ref refs/worktree/second HEAD &&
	git -C fer2/second for-each-ref --format="%(refname)" | grep second >actual &&
	cat >expected <<-\EOF &&
	refs/bisect/second
	refs/heads/second
	refs/rewritten/second
	refs/worktree/second
	EOF
	test_cmp expected actual
'

test_done
