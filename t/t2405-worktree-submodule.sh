#!/bin/sh

test_description='Combination of submodules and multiple worktrees'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

base_path=$(pwd -P)

test_expect_success 'setup: create origin repos'  '
	git init origin/sub &&
	test_commit -C origin/sub file1 &&
	git init origin/main &&
	test_commit -C origin/main first &&
	git -C origin/main submodule add ../sub &&
	git -C origin/main commit -m "add sub" &&
	test_commit -C origin/sub "file1 updated" file1 file1updated file1updated &&
	git -C origin/main/sub pull &&
	git -C origin/main add sub &&
	git -C origin/main commit -m "sub updated"
'

test_expect_success 'setup: clone superproject to create main worktree' '
	git clone --recursive "$base_path/origin/main" main
'

rev1_hash_main=$(git --git-dir=origin/main/.git show --pretty=format:%h -q "HEAD~1")
rev1_hash_sub=$(git --git-dir=origin/sub/.git show --pretty=format:%h -q "HEAD~1")

test_expect_success 'add superproject worktree' '
	git -C main worktree add "$base_path/worktree" "$rev1_hash_main"
'

test_expect_failure 'submodule is checked out just after worktree add' '
	git -C worktree diff --submodule main"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'add superproject worktree and initialize submodules' '
	git -C main worktree add "$base_path/worktree-submodule-update" "$rev1_hash_main" &&
	git -C worktree-submodule-update submodule update
'

test_expect_success 'submodule is checked out just after submodule update in linked worktree' '
	git -C worktree-submodule-update diff --submodule main"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'add superproject worktree and manually add submodule worktree' '
	git -C main worktree add "$base_path/linked_submodule" "$rev1_hash_main" &&
	git -C main/sub worktree add "$base_path/linked_submodule/sub" "$rev1_hash_sub"
'

test_expect_success 'submodule is checked out after manually adding submodule worktree' '
	git -C linked_submodule diff --submodule main"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'checkout --recurse-submodules uses $GIT_DIR for submodules in a linked worktree' '
	git -C main worktree add "$base_path/checkout-recurse" --detach  &&
	git -C checkout-recurse submodule update --init &&
	echo "gitdir: ../../main/.git/worktrees/checkout-recurse/modules/sub" >expect-gitfile &&
	cat checkout-recurse/sub/.git >actual-gitfile &&
	test_cmp expect-gitfile actual-gitfile &&
	git -C main/sub rev-parse HEAD >expect-head-main &&
	git -C checkout-recurse checkout --recurse-submodules HEAD~1 &&
	cat checkout-recurse/sub/.git >actual-gitfile &&
	git -C main/sub rev-parse HEAD >actual-head-main &&
	test_cmp expect-gitfile actual-gitfile &&
	test_cmp expect-head-main actual-head-main
'

test_expect_success 'core.worktree is removed in $GIT_DIR/modules/<name>/config, not in $GIT_COMMON_DIR/modules/<name>/config' '
	echo "../../../sub" >expect-main &&
	git -C main/sub config --get core.worktree >actual-main &&
	test_cmp expect-main actual-main &&
	echo "../../../../../../checkout-recurse/sub" >expect-linked &&
	git -C checkout-recurse/sub config --get core.worktree >actual-linked &&
	test_cmp expect-linked actual-linked &&
	git -C checkout-recurse checkout --recurse-submodules first &&
	test_expect_code 1 git -C main/.git/worktrees/checkout-recurse/modules/sub config --get core.worktree >linked-config &&
	test_must_be_empty linked-config &&
	git -C main/sub config --get core.worktree >actual-main &&
	test_cmp expect-main actual-main
'

test_expect_success 'unsetting core.worktree does not prevent running commands directly against the submodule repository' '
	git -C main/.git/worktrees/checkout-recurse/modules/sub log
'

test_done
