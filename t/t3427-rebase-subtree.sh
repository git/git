#!/bin/sh

test_description='git rebase tests for -Xsubtree

This test runs git rebase and tests the subtree strategy.
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

commit_message() {
	git log --pretty=format:%s -1 "$1"
}

# There are a few bugs in the rebase with regards to the subtree strategy, and
# this test script tries to document them.  First, the following commit history
# is generated (the onelines are shown, time flows from left to right):
#
# master1 - master2 - master3
#                             \
# README ---------------------- Add subproject master - master4 - files_subtree/master5
#
# Where the merge moves the files master[123].t into the subdirectory
# files_subtree/ and master4 as well as files_subtree/master5 add files to that
# directory directly.
#
# Then, in subsequent test cases, `git filter-branch` is used to distill just
# the commits that touch files_subtree/. To give it a final pre-rebase touch,
# an empty commit is added on top. The pre-rebase commit history looks like
# this:
#
# Add subproject master - master4 - files_subtree/master5 - Empty commit
#
# where the root commit adds three files: master1.t, master2.t and master3.t.
#
# This commit history is then rebased onto `master3` with the
# `-Xsubtree=files_subtree` option in three different ways:
#
# 1. using `--preserve-merges`
# 2. using `--preserve-merges` and --keep-empty
# 3. without specifying a rebase backend

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
test_expect_failure REBASE_P \
	'Rebase -Xsubtree --preserve-merges --onto commit 4' '
	reset_rebase &&
	git checkout -b rebase-preserve-merges-4 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD~)" = "files_subtree/master4"
'

# FAILURE: Does not preserve master5.
test_expect_failure REBASE_P \
	'Rebase -Xsubtree --preserve-merges --onto commit 5' '
	reset_rebase &&
	git checkout -b rebase-preserve-merges-5 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD)" = "files_subtree/master5"
'

# FAILURE: Does not preserve master4.
test_expect_failure REBASE_P \
	'Rebase -Xsubtree --keep-empty --preserve-merges --onto commit 4' '
	reset_rebase &&
	git checkout -b rebase-keep-empty-4 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --keep-empty --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD~2)" = "files_subtree/master4"
'

# FAILURE: Does not preserve master5.
test_expect_failure REBASE_P \
	'Rebase -Xsubtree --keep-empty --preserve-merges --onto commit 5' '
	reset_rebase &&
	git checkout -b rebase-keep-empty-5 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --keep-empty --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD~)" = "files_subtree/master5"
'

# FAILURE: Does not preserve Empty.
test_expect_failure REBASE_P \
	'Rebase -Xsubtree --keep-empty --preserve-merges --onto empty commit' '
	reset_rebase &&
	git checkout -b rebase-keep-empty-empty master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --keep-empty --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD)" = "Empty commit"
'

# FAILURE: fatal: Could not parse object
test_expect_failure 'Rebase -Xsubtree --onto commit 4' '
	reset_rebase &&
	git checkout -b rebase-onto-4 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --onto files-master master &&
	verbose test "$(commit_message HEAD~2)" = "files_subtree/master4"
'

# FAILURE: fatal: Could not parse object
test_expect_failure 'Rebase -Xsubtree --onto commit 5' '
	reset_rebase &&
	git checkout -b rebase-onto-5 master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --onto files-master master &&
	verbose test "$(commit_message HEAD~)" = "files_subtree/master5"
'
# FAILURE: fatal: Could not parse object
test_expect_failure 'Rebase -Xsubtree --onto empty commit' '
	reset_rebase &&
	git checkout -b rebase-onto-empty master &&
	git filter-branch --prune-empty -f --subdirectory-filter files_subtree &&
	git commit -m "Empty commit" --allow-empty &&
	git rebase -Xsubtree=files_subtree --onto files-master master &&
	verbose test "$(commit_message HEAD)" = "Empty commit"
'

test_done
