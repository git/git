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

test_done
