#!/bin/sh

test_description='Combination of submodules and multiple worktrees'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

base_path=$(pwd -P)

test_expect_success 'setup: create origin repos'  '
	but init origin/sub &&
	test_cummit -C origin/sub file1 &&
	but init origin/main &&
	test_cummit -C origin/main first &&
	but -C origin/main submodule add ../sub &&
	but -C origin/main cummit -m "add sub" &&
	test_cummit -C origin/sub "file1 updated" file1 file1updated file1updated &&
	but -C origin/main/sub pull &&
	but -C origin/main add sub &&
	but -C origin/main cummit -m "sub updated"
'

test_expect_success 'setup: clone superproject to create main worktree' '
	but clone --recursive "$base_path/origin/main" main
'

rev1_hash_main=$(but --but-dir=origin/main/.but show --pretty=format:%h -q "HEAD~1")
rev1_hash_sub=$(but --but-dir=origin/sub/.but show --pretty=format:%h -q "HEAD~1")

test_expect_success 'add superproject worktree' '
	but -C main worktree add "$base_path/worktree" "$rev1_hash_main"
'

test_expect_failure 'submodule is checked out just after worktree add' '
	but -C worktree diff --submodule main"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'add superproject worktree and initialize submodules' '
	but -C main worktree add "$base_path/worktree-submodule-update" "$rev1_hash_main" &&
	but -C worktree-submodule-update submodule update
'

test_expect_success 'submodule is checked out just after submodule update in linked worktree' '
	but -C worktree-submodule-update diff --submodule main"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'add superproject worktree and manually add submodule worktree' '
	but -C main worktree add "$base_path/linked_submodule" "$rev1_hash_main" &&
	but -C main/sub worktree add "$base_path/linked_submodule/sub" "$rev1_hash_sub"
'

test_expect_success 'submodule is checked out after manually adding submodule worktree' '
	but -C linked_submodule diff --submodule main"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'checkout --recurse-submodules uses $BUT_DIR for submodules in a linked worktree' '
	but -C main worktree add "$base_path/checkout-recurse" --detach  &&
	but -C checkout-recurse submodule update --init &&
	echo "butdir: ../../main/.but/worktrees/checkout-recurse/modules/sub" >expect-butfile &&
	cat checkout-recurse/sub/.but >actual-butfile &&
	test_cmp expect-butfile actual-butfile &&
	but -C main/sub rev-parse HEAD >expect-head-main &&
	but -C checkout-recurse checkout --recurse-submodules HEAD~1 &&
	cat checkout-recurse/sub/.but >actual-butfile &&
	but -C main/sub rev-parse HEAD >actual-head-main &&
	test_cmp expect-butfile actual-butfile &&
	test_cmp expect-head-main actual-head-main
'

test_expect_success 'core.worktree is removed in $BUT_DIR/modules/<name>/config, not in $BUT_COMMON_DIR/modules/<name>/config' '
	echo "../../../sub" >expect-main &&
	but -C main/sub config --get core.worktree >actual-main &&
	test_cmp expect-main actual-main &&
	echo "../../../../../../checkout-recurse/sub" >expect-linked &&
	but -C checkout-recurse/sub config --get core.worktree >actual-linked &&
	test_cmp expect-linked actual-linked &&
	but -C checkout-recurse checkout --recurse-submodules first &&
	test_expect_code 1 but -C main/.but/worktrees/checkout-recurse/modules/sub config --get core.worktree >linked-config &&
	test_must_be_empty linked-config &&
	but -C main/sub config --get core.worktree >actual-main &&
	test_cmp expect-main actual-main
'

test_expect_success 'unsetting core.worktree does not prevent running commands directly against the submodule repository' '
	but -C main/.but/worktrees/checkout-recurse/modules/sub log
'

test_done
