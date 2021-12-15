#!/bin/sh

test_description="config file in multi worktree"

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit start
'

test_expect_success 'config --worktree in single worktree' '
	git config --worktree foo.bar true &&
	test_cmp_config true foo.bar
'

test_expect_success 'add worktrees' '
	git worktree add wt1 &&
	git worktree add wt2
'

test_expect_success 'config --worktree without extension' '
	test_must_fail git config --worktree foo.bar false
'

test_expect_success 'enable worktreeConfig extension' '
	git config core.repositoryformatversion 1 &&
	git config extensions.worktreeConfig true &&
	test_cmp_config true extensions.worktreeConfig &&
	test_cmp_config 1 core.repositoryformatversion
'

test_expect_success 'config is shared as before' '
	git config this.is shared &&
	test_cmp_config shared this.is &&
	test_cmp_config -C wt1 shared this.is &&
	test_cmp_config -C wt2 shared this.is
'

test_expect_success 'config is shared (set from another worktree)' '
	git -C wt1 config that.is also-shared &&
	test_cmp_config also-shared that.is &&
	test_cmp_config -C wt1 also-shared that.is &&
	test_cmp_config -C wt2 also-shared that.is
'

test_expect_success 'config private to main worktree' '
	git config --worktree this.is for-main &&
	test_cmp_config for-main this.is &&
	test_cmp_config -C wt1 shared this.is &&
	test_cmp_config -C wt2 shared this.is
'

test_expect_success 'config private to linked worktree' '
	git -C wt1 config --worktree this.is for-wt1 &&
	test_cmp_config for-main this.is &&
	test_cmp_config -C wt1 for-wt1 this.is &&
	test_cmp_config -C wt2 shared this.is
'

test_expect_success 'core.bare no longer for main only' '
	test_config core.bare true &&
	test "$(git rev-parse --is-bare-repository)" = true &&
	test "$(git -C wt1 rev-parse --is-bare-repository)" = true &&
	test "$(git -C wt2 rev-parse --is-bare-repository)" = true
'

test_expect_success 'per-worktree core.bare is picked up' '
	git -C wt1 config --worktree core.bare true &&
	test "$(git rev-parse --is-bare-repository)" = false &&
	test "$(git -C wt1 rev-parse --is-bare-repository)" = true &&
	test "$(git -C wt2 rev-parse --is-bare-repository)" = false
'

test_expect_success 'config.worktree no longer read without extension' '
	git config --unset extensions.worktreeConfig &&
	test_cmp_config shared this.is &&
	test_cmp_config -C wt1 shared this.is &&
	test_cmp_config -C wt2 shared this.is
'

test_done
