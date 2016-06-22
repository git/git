#!/bin/sh

test_description='git rebase tests for -Xsubtree

This test runs git rebase and tests the subtree strategy.
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

last_commit_message() {
	git log --pretty=format:%s -1
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
	git reset $commit &&
	(
		cd files_subtree &&
		test_commit master4
	) &&
	test_commit files_subtree/master5
'

# FAILURE: Does not preserve master4 and master5.
test_expect_failure 'Rebase -Xsubtree --preserve-merges --onto' '
	reset_rebase &&
	git checkout -b rebase-preserve-merges master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --preserve-merges --onto files-master master &&
	verbose test "$(last_commit_message)" = "files_subtree/master5"
'

# FAILURE: Does not preserve master4, master5 and empty.
test_expect_failure 'Rebase -Xsubtree --keep-empty --preserve-merges --onto' '
	reset_rebase &&
	git checkout -b rebase-keep-empty master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --keep-empty --preserve-merges --onto files-master master &&
	verbose test "$(last_commit_message)" = "Empty commit"
'

# FAILURE: fatal: Could not parse object
test_expect_failure 'Rebase -Xsubtree --onto' '
	reset_rebase &&
	git checkout -b rebase-onto master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --onto files-master master &&
	verbose test "$(last_commit_message)" = "Empty commit"
'

test_done
