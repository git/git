#!/bin/sh

test_description='git branch2 copies the working tree into .git/branch2'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'prepare repository with tracked and untracked files' '
	test_commit initial tracked &&
	git branch dev &&
	echo untracked >untracked.txt &&
	mkdir dir &&
	echo nested >dir/nested.txt
'

test_expect_success 'git branch2 checks out the target branch and copies the working tree without .git' '
	git branch2 dev &&
	test "$(git branch --show-current)" = dev &&
	test_path_is_dir .git/branch2/dev &&
	test_cmp tracked .git/branch2/dev/tracked &&
	test_cmp untracked.txt .git/branch2/dev/untracked.txt &&
	test_cmp dir/nested.txt .git/branch2/dev/dir/nested.txt &&
	test_path_is_missing .git/branch2/dev/.git
'

test_expect_success 'git branch2 rejects duplicate destination' '
	test_must_fail git branch2 dev
'

test_expect_success 'git branch2 rejects invalid branch-like names' '
	test_must_fail git branch2 ../escape
'

test_expect_success 'git branch2 --sync updates the matching branch worktree' '
	echo branch2 >.git/branch2/dev/tracked &&
	rm .git/branch2/dev/untracked.txt &&
	echo added >.git/branch2/dev/added.txt &&
	git switch main &&
	git branch2 dev --sync &&
	test "$(git branch --show-current)" = dev &&
	test_cmp .git/branch2/dev/tracked tracked &&
	test_path_is_missing untracked.txt &&
	test_cmp .git/branch2/dev/added.txt added.txt
'

test_expect_success 'git branch2 --fetch refreshes the snapshot from the checked out branch' '
	git reset --hard &&
	git clean -fd &&
	git switch dev &&
	echo dev-refresh >tracked &&
	git commit -am "dev refresh" &&
	git switch main &&
	git branch2 dev --fetch &&
	test "$(git branch --show-current)" = dev &&
	test_cmp tracked .git/branch2/dev/tracked &&
	test_path_is_missing .git/branch2/dev/added.txt &&
	test_path_is_missing .git/branch2/dev/untracked.txt &&
	test_path_is_missing .git/branch2/dev/dir
'

test_expect_success 'git branch2 refuses branch switches from a dirty worktree' '
	git switch main &&
	echo dirty >tracked &&
	test_must_fail git branch2 dev --sync
'

test_done
