#!/bin/sh

test_description='fetch follows remote-tracking branches correctly'

. ./test-lib.sh

test_expect_success setup '
	>file &&
	but add . &&
	test_tick &&
	but cummit -m Initial &&
	but branch b-0 &&
	but branch b1 &&
	but branch b/one &&
	test_create_repo other &&
	(
		cd other &&
		but config remote.origin.url .. &&
		but config remote.origin.fetch "+refs/heads/b/*:refs/remotes/b/*"
	)
'

test_expect_success fetch '
	(
		cd other && but fetch origin &&
		test "$(but for-each-ref --format="%(refname)")" = refs/remotes/b/one
	)
'

test_done
