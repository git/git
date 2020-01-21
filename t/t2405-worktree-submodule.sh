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

test_expect_success 'setup: clone' '
	mkdir clone &&
	git -C clone clone --recursive "$base_path/origin/main"
'

rev1_hash_main=$(git --git-dir=origin/main/.git show --pretty=format:%h -q "HEAD~1")
rev1_hash_sub=$(git --git-dir=origin/sub/.git show --pretty=format:%h -q "HEAD~1")

test_expect_success 'checkout main' '
	mkdir default_checkout &&
	git -C clone/main worktree add "$base_path/default_checkout/main" "$rev1_hash_main"
'

test_expect_failure 'can see submodule diffs just after checkout' '
	git -C default_checkout/main diff --submodule master"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'checkout main and initialize independent clones' '
	mkdir fully_cloned_submodule &&
	git -C clone/main worktree add "$base_path/fully_cloned_submodule/main" "$rev1_hash_main" &&
	git -C fully_cloned_submodule/main submodule update
'

test_expect_success 'can see submodule diffs after independent cloning' '
	git -C fully_cloned_submodule/main diff --submodule master"^!" >out &&
	grep "file1 updated" out
'

test_expect_success 'checkout sub manually' '
	mkdir linked_submodule &&
	git -C clone/main worktree add "$base_path/linked_submodule/main" "$rev1_hash_main" &&
	git -C clone/main/sub worktree add "$base_path/linked_submodule/main/sub" "$rev1_hash_sub"
'

test_expect_success 'can see submodule diffs after manual checkout of linked submodule' '
	git -C linked_submodule/main diff --submodule master"^!" >out &&
	grep "file1 updated" out
'

test_done
