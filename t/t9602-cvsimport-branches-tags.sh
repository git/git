#!/bin/sh

# A description of the repository used for this test can be found in
# t9602/README.

test_description='git cvsimport handling of branches and tags'
. ./lib-cvs.sh

setup_cvs_test_repository t9602

test_expect_success PERL 'import module' '

	git cvsimport -C module-git module

'

test_expect_success PERL 'test branch master' '

	test_cmp_branch_tree master

'

test_expect_success PERL 'test branch vendorbranch' '

	test_cmp_branch_tree vendorbranch

'

test_expect_failure PERL 'test branch B_FROM_INITIALS' '

	test_cmp_branch_tree B_FROM_INITIALS

'

test_expect_failure PERL 'test branch B_FROM_INITIALS_BUT_ONE' '

	test_cmp_branch_tree B_FROM_INITIALS_BUT_ONE

'

test_expect_failure PERL 'test branch B_MIXED' '

	test_cmp_branch_tree B_MIXED

'

test_expect_success PERL 'test branch B_SPLIT' '

	test_cmp_branch_tree B_SPLIT

'

test_expect_failure PERL 'test tag vendortag' '

	test_cmp_branch_tree vendortag

'

test_expect_success PERL 'test tag T_ALL_INITIAL_FILES' '

	test_cmp_branch_tree T_ALL_INITIAL_FILES

'

test_expect_failure PERL 'test tag T_ALL_INITIAL_FILES_BUT_ONE' '

	test_cmp_branch_tree T_ALL_INITIAL_FILES_BUT_ONE

'

test_expect_failure PERL 'test tag T_MIXED' '

	test_cmp_branch_tree T_MIXED

'


test_done
