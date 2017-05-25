#!/bin/sh

test_description='git rabassa tests for -Xsubtree

This test runs git rabassa and tests the subtree strategy.
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rabassa.sh

commit_message() {
	git log --pretty=format:%s -1 "$1"
}

test_expect_success 'setup' '
	test_commit README &&
	mkdir files &&
	(
		cd files &&
		git init &&
		test_commit master1 &&
		test_commit master2 &&
		test_commit master3
	) &&
	git fetch files master &&
	git branch files-master FETCH_HEAD &&
	git read-tree --prefix=files_subtree files-master &&
	git checkout -- files_subtree &&
	tree=$(git write-tree) &&
	head=$(git rev-parse HEAD) &&
	rev=$(git rev-parse --verify files-master^0) &&
	commit=$(git commit-tree -p $head -p $rev -m "Add subproject master" $tree) &&
	git update-ref HEAD $commit &&
	(
		cd files_subtree &&
		test_commit master4
	) &&
	test_commit files_subtree/master5
'

# FAILURE: Does not preserve master4.
test_expect_failure 'Rebase -Xsubtree --preserve-merges --onto commit 4' '
	reset_rabassa &&
	git checkout -b rabassa-preserve-merges-4 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rabassa -Xsubtree=files_subtree --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD~)" = "files_subtree/master4"
'

# FAILURE: Does not preserve master5.
test_expect_failure 'Rebase -Xsubtree --preserve-merges --onto commit 5' '
	reset_rabassa &&
	git checkout -b rabassa-preserve-merges-5 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rabassa -Xsubtree=files_subtree --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD)" = "files_subtree/master5"
'

# FAILURE: Does not preserve master4.
test_expect_failure 'Rebase -Xsubtree --keep-empty --preserve-merges --onto commit 4' '
	reset_rabassa &&
	git checkout -b rabassa-keep-empty-4 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rabassa -Xsubtree=files_subtree --keep-empty --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD~2)" = "files_subtree/master4"
'

# FAILURE: Does not preserve master5.
test_expect_failure 'Rebase -Xsubtree --keep-empty --preserve-merges --onto commit 5' '
	reset_rabassa &&
	git checkout -b rabassa-keep-empty-5 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rabassa -Xsubtree=files_subtree --keep-empty --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD~)" = "files_subtree/master5"
'

# FAILURE: Does not preserve Empty.
test_expect_failure 'Rebase -Xsubtree --keep-empty --preserve-merges --onto empty commit' '
	reset_rabassa &&
	git checkout -b rabassa-keep-empty-empty master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rabassa -Xsubtree=files_subtree --keep-empty --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD)" = "Empty commit"
'

# FAILURE: fatal: Could not parse object
test_expect_failure 'Rebase -Xsubtree --onto commit 4' '
	reset_rabassa &&
	git checkout -b rabassa-onto-4 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rabassa -Xsubtree=files_subtree --onto files-master master &&
	verbose test "$(commit_message HEAD~2)" = "files_subtree/master4"
'

# FAILURE: fatal: Could not parse object
test_expect_failure 'Rebase -Xsubtree --onto commit 5' '
	reset_rabassa &&
	git checkout -b rabassa-onto-5 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rabassa -Xsubtree=files_subtree --onto files-master master &&
	verbose test "$(commit_message HEAD~)" = "files_subtree/master5"
'
# FAILURE: fatal: Could not parse object
test_expect_failure 'Rebase -Xsubtree --onto empty commit' '
	reset_rabassa &&
	git checkout -b rabassa-onto-empty master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rabassa -Xsubtree=files_subtree --onto files-master master &&
	verbose test "$(commit_message HEAD)" = "Empty commit"
'

test_done
