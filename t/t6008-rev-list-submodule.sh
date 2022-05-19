#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='but rev-list involving submodules that this repo has'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	: > file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	echo 1 > file &&
	test_tick &&
	but cummit -m second file &&
	echo 2 > file &&
	test_tick &&
	but cummit -m third file &&

	rm .but/index &&

	: > super-file &&
	but add super-file &&
	but submodule add "$(pwd)" sub &&
	but symbolic-ref HEAD refs/heads/super &&
	test_tick &&
	but cummit -m super-initial &&
	echo 1 > super-file &&
	test_tick &&
	but cummit -m super-first super-file &&
	echo 2 > super-file &&
	test_tick &&
	but cummit -m super-second super-file
'

test_expect_success "Ilari's test" '
	but rev-list --objects super main ^super^
'

test_done
