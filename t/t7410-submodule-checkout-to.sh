#!/bin/sh

test_description='Combination of submodules and multiple workdirs'

. ./test-lib.sh

base_path=$(pwd -P)

test_expect_success 'setup: make origin' \
    'mkdir -p origin/sub && ( cd origin/sub && git init &&
	echo file1 >file1 &&
	git add file1 &&
	git commit -m file1 ) &&
    mkdir -p origin/main && ( cd origin/main && git init &&
	git submodule add ../sub &&
	git commit -m "add sub" ) &&
    ( cd origin/sub &&
	echo file1updated >file1 &&
	git add file1 &&
	git commit -m "file1 updated" ) &&
    ( cd origin/main/sub && git pull ) &&
    ( cd origin/main &&
	git add sub &&
	git commit -m "sub updated" )'

test_expect_success 'setup: clone' \
    'mkdir clone && ( cd clone &&
	git clone --recursive "$base_path/origin/main")'

rev1_hash_main=$(git --git-dir=origin/main/.git show --pretty=format:%h -q "HEAD~1")
rev1_hash_sub=$(git --git-dir=origin/sub/.git show --pretty=format:%h -q "HEAD~1")

test_expect_success 'checkout main' \
    'mkdir default_checkout &&
    (cd clone/main &&
	git worktree add "$base_path/default_checkout/main" "$rev1_hash_main")'

test_expect_failure 'can see submodule diffs just after checkout' \
    '(cd default_checkout/main && git diff --submodule master"^!" | grep "file1 updated")'

test_expect_success 'checkout main and initialize independed clones' \
    'mkdir fully_cloned_submodule &&
    (cd clone/main &&
	git worktree add "$base_path/fully_cloned_submodule/main" "$rev1_hash_main") &&
    (cd fully_cloned_submodule/main && git submodule update)'

test_expect_success 'can see submodule diffs after independed cloning' \
    '(cd fully_cloned_submodule/main && git diff --submodule master"^!" | grep "file1 updated")'

test_expect_success 'checkout sub manually' \
    'mkdir linked_submodule &&
    (cd clone/main &&
	git worktree add "$base_path/linked_submodule/main" "$rev1_hash_main") &&
    (cd clone/main/sub &&
	git worktree add "$base_path/linked_submodule/main/sub" "$rev1_hash_sub")'

test_expect_success 'can see submodule diffs after manual checkout of linked submodule' \
    '(cd linked_submodule/main && git diff --submodule master"^!" | grep "file1 updated")'

test_done
