#!/bin/sh

test_description='test worktree ref store api'

. ./test-lib.sh

RWT="test-tool ref-store worktree:wt"
RMAIN="test-tool ref-store worktree:main"

test_expect_success 'setup' '
	test_commit first &&
	git worktree add -b wt-master wt &&
	(
		cd wt &&
		test_commit second
	)
'

test_expect_success 'resolve_ref(<shared-ref>)' '
	SHA1=`git rev-parse master` &&
	echo "$SHA1 refs/heads/master 0x0" >expected &&
	$RWT resolve-ref refs/heads/master 0 >actual &&
	test_cmp expected actual &&
	$RMAIN resolve-ref refs/heads/master 0 >actual &&
	test_cmp expected actual
'

test_expect_success 'resolve_ref(<per-worktree-ref>)' '
	SHA1=`git -C wt rev-parse HEAD` &&
	echo "$SHA1 refs/heads/wt-master 0x1" >expected &&
	$RWT resolve-ref HEAD 0 >actual &&
	test_cmp expected actual &&

	SHA1=`git rev-parse HEAD` &&
	echo "$SHA1 refs/heads/master 0x1" >expected &&
	$RMAIN resolve-ref HEAD 0 >actual &&
	test_cmp expected actual
'

test_expect_success 'create_symref(FOO, refs/heads/master)' '
	$RWT create-symref FOO refs/heads/master nothing &&
	echo refs/heads/master >expected &&
	git -C wt symbolic-ref FOO >actual &&
	test_cmp expected actual &&

	$RMAIN create-symref FOO refs/heads/wt-master nothing &&
	echo refs/heads/wt-master >expected &&
	git symbolic-ref FOO >actual &&
	test_cmp expected actual
'

test_expect_success 'for_each_reflog()' '
	echo $ZERO_OID > .git/logs/PSEUDO-MAIN &&
	mkdir -p     .git/logs/refs/bisect &&
	echo $ZERO_OID > .git/logs/refs/bisect/random &&

	echo $ZERO_OID > .git/worktrees/wt/logs/PSEUDO-WT &&
	mkdir -p     .git/worktrees/wt/logs/refs/bisect &&
	echo $ZERO_OID > .git/worktrees/wt/logs/refs/bisect/wt-random &&

	$RWT for-each-reflog | cut -d" " -f 2- | sort >actual &&
	cat >expected <<-\EOF &&
	HEAD 0x1
	PSEUDO-WT 0x0
	refs/bisect/wt-random 0x0
	refs/heads/master 0x0
	refs/heads/wt-master 0x0
	EOF
	test_cmp expected actual &&

	$RMAIN for-each-reflog | cut -d" " -f 2- | sort >actual &&
	cat >expected <<-\EOF &&
	HEAD 0x1
	PSEUDO-MAIN 0x0
	refs/bisect/random 0x0
	refs/heads/master 0x0
	refs/heads/wt-master 0x0
	EOF
	test_cmp expected actual
'

test_done
