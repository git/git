#!/bin/sh

# A description of the repository used for this test can be found in
# t9602/README.

test_description='git cvsimport handling of branches and tags'
. ./lib-cvs.sh

CVSROOT="$TEST_DIRECTORY"/t9602/cvsroot
export CVSROOT

test_expect_success 'import module' '

	git cvsimport -C module-git module

'

test_expect_success 'test branch master' '

	test_cmp_branch_tree master

'

test_expect_success 'test branch vendorbranch' '

	test_cmp_branch_tree vendorbranch

'

test_expect_failure 'test branch B_FROM_INITIALS' '

	test_cmp_branch_tree B_FROM_INITIALS

'

test_expect_failure 'test branch B_FROM_INITIALS_BUT_ONE' '

	test_cmp_branch_tree B_FROM_INITIALS_BUT_ONE

'

test_expect_failure 'test branch B_MIXED' '

	test_cmp_branch_tree B_MIXED

'

test_expect_success 'test branch B_SPLIT' '

	test_cmp_branch_tree B_SPLIT

'

test_expect_failure 'test tag vendortag' '

	test_cmp_branch_tree vendortag

'

test_expect_success 'test tag T_ALL_INITIAL_FILES' '

	test_cmp_branch_tree T_ALL_INITIAL_FILES

'

test_expect_failure 'test tag T_ALL_INITIAL_FILES_BUT_ONE' '

	test_cmp_branch_tree T_ALL_INITIAL_FILES_BUT_ONE

'

test_expect_failure 'test tag T_MIXED' '

	test_cmp_branch_tree T_MIXED

'


test_done
