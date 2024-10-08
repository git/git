#!/bin/sh

test_description='test worktrees linked with relative paths'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'links worktrees with relative paths' '
	test_when_finished rm -rf repo &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		git worktree add wt1 &&
		echo "../../../wt1/.git" >expected_gitdir &&
		cat .git/worktrees/wt1/gitdir >actual_gitdir &&
		echo "gitdir: ../.git/worktrees/wt1" >expected_git &&
		cat wt1/.git >actual_git &&
		test_cmp expected_gitdir actual_gitdir &&
		test_cmp expected_git actual_git
	)
'

test_expect_success 'move repo without breaking relative internal links' '
	test_when_finished rm -rf repo moved &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		git worktree add wt1 &&
		cd .. &&
		mv repo moved &&
		cd moved/wt1 &&
		git status >out 2>err &&
		test_must_be_empty err
	)
'

test_done
