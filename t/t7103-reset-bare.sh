#!/bin/sh

test_description='git reset in a bare repository'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup non-bare' '
	echo one >file &&
	git add file &&
	git commit -m one &&
	echo two >file &&
	git commit -a -m two
'

test_expect_success '"hard" reset requires a worktree' '
	(cd .git &&
	 test_must_fail git reset --hard)
'

test_expect_success '"merge" reset requires a worktree' '
	(cd .git &&
	 test_must_fail git reset --merge)
'

test_expect_success '"keep" reset requires a worktree' '
	(cd .git &&
	 test_must_fail git reset --keep)
'

test_expect_success '"mixed" reset is ok' '
	(cd .git && git reset)
'

test_expect_success '"soft" reset is ok' '
	(cd .git && git reset --soft)
'

test_expect_success 'hard reset works with GIT_WORK_TREE' '
	mkdir worktree &&
	GIT_WORK_TREE=$PWD/worktree GIT_DIR=$PWD/.git git reset --hard &&
	test_cmp file worktree/file
'

test_expect_success 'setup bare' '
	git clone --bare . bare.git &&
	cd bare.git
'

test_expect_success '"hard" reset is not allowed in bare' '
	test_must_fail git reset --hard HEAD^
'

test_expect_success '"merge" reset is not allowed in bare' '
	test_must_fail git reset --merge HEAD^
'

test_expect_success '"keep" reset is not allowed in bare' '
	test_must_fail git reset --keep HEAD^
'

test_expect_success '"mixed" reset is not allowed in bare' '
	test_must_fail git reset --mixed HEAD^
'

test_expect_success '"soft" reset is allowed in bare' '
	git reset --soft HEAD^ &&
	git show --pretty=format:%s >out &&
	echo one >expect &&
	head -n 1 out >actual &&
	test_cmp expect actual
'

test_done
