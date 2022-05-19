#!/bin/sh

test_description='per-worktree refs'

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit initial &&
	test_cummit wt1 &&
	test_cummit wt2 &&
	but worktree add wt1 wt1 &&
	but worktree add wt2 wt2 &&
	but checkout initial &&
	but update-ref refs/worktree/foo HEAD &&
	but -C wt1 update-ref refs/worktree/foo HEAD &&
	but -C wt2 update-ref refs/worktree/foo HEAD
'

# The 'packed-refs' file is stored directly in .but/. This means it is global
# to the repository, and can only contain refs that are shared across all
# worktrees.
test_expect_success REFFILES 'refs/worktree must not be packed' '
	but pack-refs --all &&
	test_path_is_missing .but/refs/tags/wt1 &&
	test_path_is_file .but/refs/worktree/foo &&
	test_path_is_file .but/worktrees/wt1/refs/worktree/foo &&
	test_path_is_file .but/worktrees/wt2/refs/worktree/foo
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
	test_when_finished but update-ref -d refs/heads/main-worktree/HEAD &&
	but update-ref refs/heads/main-worktree/HEAD $(but rev-parse HEAD) &&
	but rev-parse main-worktree/HEAD 2>warn &&
	grep "main-worktree/HEAD.*ambiguous" warn
'

test_expect_success 'resolve worktrees/xx/HEAD' '
	test_cmp_rev worktrees/wt1/HEAD wt1 &&
	( cd wt1 && test_cmp_rev worktrees/wt1/HEAD wt1 ) &&
	( cd wt2 && test_cmp_rev worktrees/wt1/HEAD wt1 )
'

test_expect_success 'ambiguous worktrees/xx/HEAD' '
	but update-ref refs/heads/worktrees/wt1/HEAD $(but rev-parse HEAD) &&
	test_when_finished but update-ref -d refs/heads/worktrees/wt1/HEAD &&
	but rev-parse worktrees/wt1/HEAD 2>warn &&
	grep "worktrees/wt1/HEAD.*ambiguous" warn
'

test_expect_success 'reflog of main-worktree/HEAD' '
	but reflog HEAD | sed "s/HEAD/main-worktree\/HEAD/" >expected &&
	but reflog main-worktree/HEAD >actual &&
	test_cmp expected actual &&
	but -C wt1 reflog main-worktree/HEAD >actual.wt1 &&
	test_cmp expected actual.wt1
'

test_expect_success 'reflog of worktrees/xx/HEAD' '
	but -C wt2 reflog HEAD | sed "s/HEAD/worktrees\/wt2\/HEAD/" >expected &&
	but reflog worktrees/wt2/HEAD >actual &&
	test_cmp expected actual &&
	but -C wt1 reflog worktrees/wt2/HEAD >actual.wt1 &&
	test_cmp expected actual.wt1 &&
	but -C wt2 reflog worktrees/wt2/HEAD >actual.wt2 &&
	test_cmp expected actual.wt2
'

test_expect_success 'for-each-ref from main worktree' '
	mkdir fer1 &&
	but -C fer1 init repo &&
	test_cummit -C fer1/repo initial &&
	but -C fer1/repo worktree add ../second &&
	but -C fer1/repo update-ref refs/bisect/first HEAD &&
	but -C fer1/repo update-ref refs/rewritten/first HEAD &&
	but -C fer1/repo update-ref refs/worktree/first HEAD &&
	but -C fer1/repo for-each-ref --format="%(refname)" | grep first >actual &&
	cat >expected <<-\EOF &&
	refs/bisect/first
	refs/rewritten/first
	refs/worktree/first
	EOF
	test_cmp expected actual
'

test_expect_success 'for-each-ref from linked worktree' '
	mkdir fer2 &&
	but -C fer2 init repo &&
	test_cummit -C fer2/repo initial &&
	but -C fer2/repo worktree add ../second &&
	but -C fer2/second update-ref refs/bisect/second HEAD &&
	but -C fer2/second update-ref refs/rewritten/second HEAD &&
	but -C fer2/second update-ref refs/worktree/second HEAD &&
	but -C fer2/second for-each-ref --format="%(refname)" | grep second >actual &&
	cat >expected <<-\EOF &&
	refs/bisect/second
	refs/heads/second
	refs/rewritten/second
	refs/worktree/second
	EOF
	test_cmp expected actual
'

test_done
