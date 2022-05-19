#!/bin/sh

test_description='but reset in a bare repository'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup non-bare' '
	echo one >file &&
	but add file &&
	but cummit -m one &&
	echo two >file &&
	but cummit -a -m two
'

test_expect_success '"hard" reset requires a worktree' '
	(cd .but &&
	 test_must_fail but reset --hard)
'

test_expect_success '"merge" reset requires a worktree' '
	(cd .but &&
	 test_must_fail but reset --merge)
'

test_expect_success '"keep" reset requires a worktree' '
	(cd .but &&
	 test_must_fail but reset --keep)
'

test_expect_success '"mixed" reset is ok' '
	(cd .but && but reset)
'

test_expect_success '"soft" reset is ok' '
	(cd .but && but reset --soft)
'

test_expect_success 'hard reset works with BUT_WORK_TREE' '
	mkdir worktree &&
	BUT_WORK_TREE=$PWD/worktree BUT_DIR=$PWD/.but but reset --hard &&
	test_cmp file worktree/file
'

test_expect_success 'setup bare' '
	but clone --bare . bare.but &&
	cd bare.but
'

test_expect_success '"hard" reset is not allowed in bare' '
	test_must_fail but reset --hard HEAD^
'

test_expect_success '"merge" reset is not allowed in bare' '
	test_must_fail but reset --merge HEAD^
'

test_expect_success '"keep" reset is not allowed in bare' '
	test_must_fail but reset --keep HEAD^
'

test_expect_success '"mixed" reset is not allowed in bare' '
	test_must_fail but reset --mixed HEAD^
'

test_expect_success !SANITIZE_LEAK '"soft" reset is allowed in bare' '
	but reset --soft HEAD^ &&
	but show --pretty=format:%s >out &&
	echo one >expect &&
	head -n 1 out >actual &&
	test_cmp expect actual
'

test_done
