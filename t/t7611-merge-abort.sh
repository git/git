#!/bin/sh

test_description='test aborting in-progress merges

Set up repo with conflicting and non-conflicting branches:

There are three files foo/bar/baz, and the following graph illustrates the
content of these files in each commit:

# foo/bar/baz --- foo/bar/bazz     <-- main
#             \
#              --- foo/barf/bazf   <-- conflict_branch
#               \
#                --- foo/bart/baz  <-- clean_branch

Next, test git merge --abort with the following variables:
- before/after successful merge (should fail when not in merge context)
- with/without conflicts
- clean/dirty index before merge
- clean/dirty worktree before merge
- dirty index before merge matches contents on remote branch
- changed/unchanged worktree after merge
- changed/unchanged index after merge
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	# Create the above repo
	echo foo > foo &&
	echo bar > bar &&
	echo baz > baz &&
	git add foo bar baz &&
	git commit -m initial &&
	echo bazz > baz &&
	git commit -a -m "second" &&
	git checkout -b conflict_branch HEAD^ &&
	echo barf > bar &&
	echo bazf > baz &&
	git commit -a -m "conflict" &&
	git checkout -b clean_branch HEAD^ &&
	echo bart > bar &&
	git commit -a -m "clean" &&
	git checkout main
'

pre_merge_head="$(git rev-parse HEAD)"

test_expect_success 'fails without MERGE_HEAD (unstarted merge)' '
	test_must_fail git merge --abort 2>output &&
	test_i18ngrep MERGE_HEAD output
'

test_expect_success 'fails without MERGE_HEAD (unstarted merge): .git/MERGE_HEAD sanity' '
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)"
'

test_expect_success 'fails without MERGE_HEAD (completed merge)' '
	git merge clean_branch &&
	test ! -f .git/MERGE_HEAD &&
	# Merge successfully completed
	post_merge_head="$(git rev-parse HEAD)" &&
	test_must_fail git merge --abort 2>output &&
	test_i18ngrep MERGE_HEAD output
'

test_expect_success 'fails without MERGE_HEAD (completed merge): .git/MERGE_HEAD sanity' '
	test ! -f .git/MERGE_HEAD &&
	test "$post_merge_head" = "$(git rev-parse HEAD)"
'

test_expect_success 'Forget previous merge' '
	git reset --hard "$pre_merge_head"
'

test_expect_success 'Abort after --no-commit' '
	# Redo merge, but stop before creating merge commit
	git merge --no-commit clean_branch &&
	test -f .git/MERGE_HEAD &&
	# Abort non-conflicting merge
	git merge --abort &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff)" &&
	test -z "$(git diff --staged)"
'

test_expect_success 'Abort after conflicts' '
	# Create conflicting merge
	test_must_fail git merge conflict_branch &&
	test -f .git/MERGE_HEAD &&
	# Abort conflicting merge
	git merge --abort &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff)" &&
	test -z "$(git diff --staged)"
'

test_expect_success 'Clean merge with dirty index fails' '
	echo xyzzy >> foo &&
	git add foo &&
	git diff --staged > expect &&
	test_must_fail git merge clean_branch &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff)" &&
	git diff --staged > actual &&
	test_cmp expect actual
'

test_expect_success 'Conflicting merge with dirty index fails' '
	test_must_fail git merge conflict_branch &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff)" &&
	git diff --staged > actual &&
	test_cmp expect actual
'

test_expect_success 'Reset index (but preserve worktree changes)' '
	git reset "$pre_merge_head" &&
	git diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Abort clean merge with non-conflicting dirty worktree' '
	git merge --no-commit clean_branch &&
	test -f .git/MERGE_HEAD &&
	# Abort merge
	git merge --abort &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff --staged)" &&
	git diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Abort conflicting merge with non-conflicting dirty worktree' '
	test_must_fail git merge conflict_branch &&
	test -f .git/MERGE_HEAD &&
	# Abort merge
	git merge --abort &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff --staged)" &&
	git diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Reset worktree changes' '
	git reset --hard "$pre_merge_head"
'

test_expect_success 'Fail clean merge with conflicting dirty worktree' '
	echo xyzzy >> bar &&
	git diff > expect &&
	test_must_fail git merge --no-commit clean_branch &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff --staged)" &&
	git diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Fail conflicting merge with conflicting dirty worktree' '
	test_must_fail git merge conflict_branch &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff --staged)" &&
	git diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Reset worktree changes' '
	git reset --hard "$pre_merge_head"
'

test_expect_success 'Fail clean merge with matching dirty worktree' '
	echo bart > bar &&
	git diff > expect &&
	test_must_fail git merge --no-commit clean_branch &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff --staged)" &&
	git diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Fail conflicting merge with matching dirty worktree' '
	echo barf > bar &&
	git diff > expect &&
	test_must_fail git merge conflict_branch &&
	test ! -f .git/MERGE_HEAD &&
	test "$pre_merge_head" = "$(git rev-parse HEAD)" &&
	test -z "$(git diff --staged)" &&
	git diff > actual &&
	test_cmp expect actual
'

test_done
