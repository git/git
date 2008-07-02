#!/bin/sh

test_description=clone

. ./test-lib.sh

test_expect_success setup '

	rm -fr .git &&
	test_create_repo src &&
	(
		cd src
		>file
		git add file
		git commit -m initial
	)

'

test_expect_success 'clone with excess parameters' '

	test_must_fail git clone -n "file://$(pwd)/src" dst junk

'

test_expect_success 'clone checks out files' '

	git clone src dst &&
	test -f dst/file

'

test_expect_success 'clone respects GIT_WORK_TREE' '

	GIT_WORK_TREE=worktree git clone src bare &&
	test -f bare/config &&
	test -f worktree/file

'

test_expect_success 'clone creates intermediate directories' '

	git clone src long/path/to/dst &&
	test -f long/path/to/dst/file

'

test_expect_success 'clone creates intermediate directories for bare repo' '

	git clone --bare src long/path/to/bare/dst &&
	test -f long/path/to/bare/dst/config

'

test_done
