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

	git init files &&
	test_commit -C files master1 &&
	test_commit -C files master2 &&
	test_commit -C files master3 &&

	: perform subtree merge into files_subtree/ &&
	git fetch files refs/heads/master:refs/heads/files-master &&
	git merge -s ours --no-commit --allow-unrelated-histories \
		files-master &&
	git read-tree --prefix=files_subtree -u files-master &&
	git commit -m "Add subproject master" &&

	: add two extra commits to rebase &&
	test_commit -C files_subtree master4 &&
	test_commit files_subtree/master5 &&

	git checkout -b to-rebase &&
	git fast-export --no-data HEAD -- files_subtree/ |
		sed -e "s%\([0-9a-f]\{40\} \)files_subtree/%\1%" |
		git fast-import --force --quiet &&
	git reset --hard &&
	git commit -m "Empty commit" --allow-empty
'

# FAILURE: Does not preserve master4.
test_expect_failure REBASE_P 'Rebase -Xsubtree --preserve-merges --onto commit' '
	reset_rebase &&
	git checkout -b rebase-preserve-merges to-rebase &&
	git rebase -Xsubtree=files_subtree --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD~)" = "master4" &&
	verbose test "$(commit_message HEAD)" = "files_subtree/master5"
'

# FAILURE: Does not preserve master4.
test_expect_failure REBASE_P 'Rebase -Xsubtree --keep-empty --preserve-merges --onto commit' '
	reset_rebase &&
	git checkout -b rebase-keep-empty to-rebase &&
	git rebase -Xsubtree=files_subtree --keep-empty --preserve-merges --onto files-master master &&
	verbose test "$(commit_message HEAD~2)" = "master4" &&
	verbose test "$(commit_message HEAD~)" = "files_subtree/master5" &&
	verbose test "$(commit_message HEAD)" = "Empty commit"
'

test_expect_success 'Rebase -Xsubtree --keep-empty --onto commit' '
	reset_rebase &&
	git checkout -b rebase-onto to-rebase &&
	test_must_fail git rebase -Xsubtree=files_subtree --keep-empty --onto files-master master &&
	: first pick results in no changes &&
	git rebase --continue &&
	verbose test "$(commit_message HEAD~2)" = "master4" &&
	verbose test "$(commit_message HEAD~)" = "files_subtree/master5" &&
	verbose test "$(commit_message HEAD)" = "Empty commit"
'

test_expect_success 'Rebase -Xsubtree --keep-empty --rebase-merges --onto commit' '
	reset_rebase &&
	git checkout -b rebase-merges-onto to-rebase &&
	test_must_fail git rebase -Xsubtree=files_subtree --keep-empty --rebase-merges --onto files-master --root &&
	: first pick results in no changes &&
	git rebase --continue &&
	verbose test "$(commit_message HEAD~2)" = "master4" &&
	verbose test "$(commit_message HEAD~)" = "files_subtree/master5" &&
	verbose test "$(commit_message HEAD)" = "Empty commit"
'

test_done
