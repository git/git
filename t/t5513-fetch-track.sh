#!/bin/sh

test_description='fetch follows remote tracking branches correctly'

. ./test-lib.sh

test_expect_success setup '
	>file &&
	git add . &&
	test_tick &&
	git commit -m Initial &&
	git branch b-0 &&
	git branch b1 &&
	git branch b/one &&
	test_create_repo other &&
	(
		cd other &&
		git config remote.origin.url .. &&
		git config remote.origin.fetch "+refs/heads/b/*:refs/remotes/b/*"
	)
'

test_expect_success fetch '
	(
		cd other && git fetch origin &&
		test "$(git for-each-ref --format="%(refname)")" = refs/remotes/b/one
	)
'

test_done
