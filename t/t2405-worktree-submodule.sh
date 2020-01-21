#!/bin/sh

test_description='Combination of submodules and multiple worktrees'

. ./test-lib.sh

base_path=$(pwd -P)

test_expect_success 'setup: create origin repos'  '
	git init origin/sub &&
	test_commit -C origin/sub file1 &&
	git init origin/main &&
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
	git -C worktree diff --submodule master"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'add superproject worktree and initialize submodules' '
	git -C main worktree add "$base_path/worktree-submodule-update" "$rev1_hash_main" &&
	git -C worktree-submodule-update submodule update
'

test_expect_success 'submodule is checked out just after submodule update in linked worktree' '
	git -C worktree-submodule-update diff --submodule master"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'add superproject worktree and manually add submodule worktree' '
	git -C main worktree add "$base_path/linked_submodule" "$rev1_hash_main" &&
	git -C main/sub worktree add "$base_path/linked_submodule/sub" "$rev1_hash_sub"
'

test_expect_success 'submodule is checked out after manually adding submodule worktree' '
	git -C linked_submodule diff --submodule master"^!" >out &&
	grep "file1 updated" out
'

test_done
