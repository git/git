#!/bin/sh

test_description='test worktree ref store api'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

RWT="test-tool ref-store worktree:wt"
RMAIN="test-tool ref-store worktree:main"

test_expect_success 'setup' '
	test_commit first &&
	git worktree add -b wt-main wt &&
	(
		cd wt &&
		test_commit second
	)
'

test_expect_success 'resolve_ref(<shared-ref>)' '
	SHA1=`git rev-parse main` &&
	echo "$SHA1 refs/heads/main 0x0" >expected &&
	$RWT resolve-ref refs/heads/main 0 >actual &&
	test_cmp expected actual &&
	$RMAIN resolve-ref refs/heads/main 0 >actual &&
	test_cmp expected actual
'

test_expect_success 'resolve_ref(<per-worktree-ref>)' '
	SHA1=`git -C wt rev-parse HEAD` &&
	echo "$SHA1 refs/heads/wt-main 0x1" >expected &&
	$RWT resolve-ref HEAD 0 >actual &&
	test_cmp expected actual &&

	SHA1=`git rev-parse HEAD` &&
	echo "$SHA1 refs/heads/main 0x1" >expected &&
	$RMAIN resolve-ref HEAD 0 >actual &&
	test_cmp expected actual
'

test_expect_success 'create_symref(FOO, refs/heads/main)' '
	$RWT create-symref FOO refs/heads/main nothing &&
	echo refs/heads/main >expected &&
	git -C wt symbolic-ref FOO >actual &&
	test_cmp expected actual &&

	$RMAIN create-symref FOO refs/heads/wt-main nothing &&
	echo refs/heads/wt-main >expected &&
	git symbolic-ref FOO >actual &&
	test_cmp expected actual
'

test_done
