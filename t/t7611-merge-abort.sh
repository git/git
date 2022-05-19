#!/bin/sh

test_description='test aborting in-progress merges

Set up repo with conflicting and non-conflicting branches:

There are three files foo/bar/baz, and the following graph illustrates the
content of these files in each cummit:

# foo/bar/baz --- foo/bar/bazz     <-- main
#             \
#              --- foo/barf/bazf   <-- conflict_branch
#               \
#                --- foo/bart/baz  <-- clean_branch

Next, test but merge --abort with the following variables:
- before/after successful merge (should fail when not in merge context)
- with/without conflicts
- clean/dirty index before merge
- clean/dirty worktree before merge
- dirty index before merge matches contents on remote branch
- changed/unchanged worktree after merge
- changed/unchanged index after merge
'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	# Create the above repo
	echo foo > foo &&
	echo bar > bar &&
	echo baz > baz &&
	but add foo bar baz &&
	but cummit -m initial &&
	echo bazz > baz &&
	but cummit -a -m "second" &&
	but checkout -b conflict_branch HEAD^ &&
	echo barf > bar &&
	echo bazf > baz &&
	but cummit -a -m "conflict" &&
	but checkout -b clean_branch HEAD^ &&
	echo bart > bar &&
	but cummit -a -m "clean" &&
	but checkout main
'

pre_merge_head="$(but rev-parse HEAD)"

test_expect_success 'fails without MERGE_HEAD (unstarted merge)' '
	test_must_fail but merge --abort 2>output &&
	test_i18ngrep MERGE_HEAD output
'

test_expect_success 'fails without MERGE_HEAD (unstarted merge): .but/MERGE_HEAD sanity' '
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)"
'

test_expect_success 'fails without MERGE_HEAD (completed merge)' '
	but merge clean_branch &&
	test ! -f .but/MERGE_HEAD &&
	# Merge successfully completed
	post_merge_head="$(but rev-parse HEAD)" &&
	test_must_fail but merge --abort 2>output &&
	test_i18ngrep MERGE_HEAD output
'

test_expect_success 'fails without MERGE_HEAD (completed merge): .but/MERGE_HEAD sanity' '
	test ! -f .but/MERGE_HEAD &&
	test "$post_merge_head" = "$(but rev-parse HEAD)"
'

test_expect_success 'Forget previous merge' '
	but reset --hard "$pre_merge_head"
'

test_expect_success 'Abort after --no-cummit' '
	# Redo merge, but stop before creating merge cummit
	but merge --no-cummit clean_branch &&
	test -f .but/MERGE_HEAD &&
	# Abort non-conflicting merge
	but merge --abort &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff)" &&
	test -z "$(but diff --staged)"
'

test_expect_success 'Abort after conflicts' '
	# Create conflicting merge
	test_must_fail but merge conflict_branch &&
	test -f .but/MERGE_HEAD &&
	# Abort conflicting merge
	but merge --abort &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff)" &&
	test -z "$(but diff --staged)"
'

test_expect_success 'Clean merge with dirty index fails' '
	echo xyzzy >> foo &&
	but add foo &&
	but diff --staged > expect &&
	test_must_fail but merge clean_branch &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff)" &&
	but diff --staged > actual &&
	test_cmp expect actual
'

test_expect_success 'Conflicting merge with dirty index fails' '
	test_must_fail but merge conflict_branch &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff)" &&
	but diff --staged > actual &&
	test_cmp expect actual
'

test_expect_success 'Reset index (but preserve worktree changes)' '
	but reset "$pre_merge_head" &&
	but diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Abort clean merge with non-conflicting dirty worktree' '
	but merge --no-cummit clean_branch &&
	test -f .but/MERGE_HEAD &&
	# Abort merge
	but merge --abort &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff --staged)" &&
	but diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Abort conflicting merge with non-conflicting dirty worktree' '
	test_must_fail but merge conflict_branch &&
	test -f .but/MERGE_HEAD &&
	# Abort merge
	but merge --abort &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff --staged)" &&
	but diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Reset worktree changes' '
	but reset --hard "$pre_merge_head"
'

test_expect_success 'Fail clean merge with conflicting dirty worktree' '
	echo xyzzy >> bar &&
	but diff > expect &&
	test_must_fail but merge --no-cummit clean_branch &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff --staged)" &&
	but diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Fail conflicting merge with conflicting dirty worktree' '
	test_must_fail but merge conflict_branch &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff --staged)" &&
	but diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Reset worktree changes' '
	but reset --hard "$pre_merge_head"
'

test_expect_success 'Fail clean merge with matching dirty worktree' '
	echo bart > bar &&
	but diff > expect &&
	test_must_fail but merge --no-cummit clean_branch &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff --staged)" &&
	but diff > actual &&
	test_cmp expect actual
'

test_expect_success 'Fail conflicting merge with matching dirty worktree' '
	echo barf > bar &&
	but diff > expect &&
	test_must_fail but merge conflict_branch &&
	test ! -f .but/MERGE_HEAD &&
	test "$pre_merge_head" = "$(but rev-parse HEAD)" &&
	test -z "$(but diff --staged)" &&
	but diff > actual &&
	test_cmp expect actual
'

test_done
