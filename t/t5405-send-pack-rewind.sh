#!/bin/sh

test_description='forced push to replace cummit we do not have'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	>file1 && but add file1 && test_tick &&
	but cummit -m Initial &&
	but config receive.denyCurrentBranch warn &&

	mkdir another && (
		cd another &&
		but init &&
		but fetch --update-head-ok .. main:main
	) &&

	>file2 && but add file2 && test_tick &&
	but cummit -m Second

'

test_expect_success 'non forced push should die not segfault' '

	(
		cd another &&
		test_must_fail but push .. main:main
	)

'

test_expect_success 'forced push should succeed' '

	(
		cd another &&
		but push .. +main:main
	)

'

test_done
