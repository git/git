#!/bin/sh

test_description='but rebase tests for -Xsubtree

This test runs but rebase and tests the subtree strategy.
'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

cummit_message() {
	but log --pretty=format:%s -1 "$1"
}

# There are a few bugs in the rebase with regards to the subtree strategy, and
# this test script tries to document them.  First, the following commit history
# is generated (the onelines are shown, time flows from left to right):
#
# topic_1 - topic_2 - topic_3
#                             \
# README ---------------------- Add subproject main - topic_4 - files_subtree/topic_5
#
# Where the merge moves the files topic_[123].t into the subdirectory
# files_subtree/ and topic_4 as well as files_subtree/topic_5 add files to that
# directory directly.
#
# Then, in subsequent test cases, `but filter-branch` is used to distill just
# the cummits that touch files_subtree/. To give it a final pre-rebase touch,
# an empty cummit is added on top. The pre-rebase commit history looks like
# this:
#
# Add subproject main - topic_4 - files_subtree/topic_5 - Empty cummit
#
# where the root cummit adds three files: topic_1.t, topic_2.t and topic_3.t.
#
# This commit history is then rebased onto `topic_3` with the
# `-Xsubtree=files_subtree` option in two different ways:
#
# 1. without specifying a rebase backend
# 2. using the `--rebase-merges` backend

test_expect_success 'setup' '
	test_cummit README &&

	but init files &&
	test_cummit -C files topic_1 &&
	test_cummit -C files topic_2 &&
	test_cummit -C files topic_3 &&

	: perform subtree merge into files_subtree/ &&
	but fetch files refs/heads/main:refs/heads/files-main &&
	but merge -s ours --no-cummit --allow-unrelated-histories \
		files-main &&
	but read-tree --prefix=files_subtree -u files-main &&
	but cummit -m "Add subproject main" &&

	: add two extra cummits to rebase &&
	test_cummit -C files_subtree topic_4 &&
	test_cummit files_subtree/topic_5 &&

	but checkout -b to-rebase &&
	but fast-export --no-data HEAD -- files_subtree/ |
		sed -e "s%\([0-9a-f]\{40\} \)files_subtree/%\1%" |
		but fast-import --force --quiet &&
	but reset --hard &&
	but cummit -m "Empty cummit" --allow-empty
'

test_expect_success 'Rebase -Xsubtree --empty=ask --onto cummit' '
	reset_rebase &&
	but checkout -b rebase-onto to-rebase &&
	test_must_fail but rebase -Xsubtree=files_subtree --empty=ask --onto files-main main &&
	: first pick results in no changes &&
	but rebase --skip &&
	verbose test "$(cummit_message HEAD~2)" = "topic_4" &&
	verbose test "$(cummit_message HEAD~)" = "files_subtree/topic_5" &&
	verbose test "$(cummit_message HEAD)" = "Empty cummit"
'

test_expect_success 'Rebase -Xsubtree --empty=ask --rebase-merges --onto cummit' '
	reset_rebase &&
	but checkout -b rebase-merges-onto to-rebase &&
	test_must_fail but rebase -Xsubtree=files_subtree --empty=ask --rebase-merges --onto files-main --root &&
	: first pick results in no changes &&
	but rebase --skip &&
	verbose test "$(cummit_message HEAD~2)" = "topic_4" &&
	verbose test "$(cummit_message HEAD~)" = "files_subtree/topic_5" &&
	verbose test "$(cummit_message HEAD)" = "Empty cummit"
'

test_done
