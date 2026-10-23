#!/bin/sh

test_description='Test config core.indexLockTimeout'

. ./test-lib.sh

prepare_index_lock () {
	test_when_finished "rm -f .git/index.lock" &&
	touch .git/index.lock &&
	{
		( sleep 1 && rm -f .git/index.lock ) &
	} &&
	test_when_finished "wait"
}

test_must_fail_with_index_lock () {
	test_must_fail "$@" 2>err &&
	test_grep index.lock err
}

test_expect_success 'index locked, add fails with indexLockTimeout=0' '
	prepare_index_lock &&
	touch foo &&
	test_must_fail_with_index_lock git -c core.indexLockTimeout=0 add foo
'

test_expect_success 'index locked, add waits with indexLockTimeout>0' '
	prepare_index_lock &&
	git -c core.indexLockTimeout=5000 add foo
'

test_expect_success 'index locked, commit waits with indexLockTimeout>0' '
	prepare_index_lock &&
	git -c core.indexLockTimeout=5000 commit -m init
'

test_expect_success 'index locked, checkout waits with indexLockTimeout>0' '
	prepare_index_lock &&
	git -c core.indexLockTimeout=5000 checkout HEAD
'

test_done
