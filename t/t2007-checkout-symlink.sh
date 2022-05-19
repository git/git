#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano

test_description='but checkout to switch between branches with symlink<->dir'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	mkdir frotz &&
	echo hello >frotz/filfre &&
	but add frotz/filfre &&
	test_tick &&
	but cummit -m "main has file frotz/filfre" &&

	but branch side &&

	echo goodbye >nitfol &&
	but add nitfol &&
	test_tick &&
	but cummit -m "main adds file nitfol" &&

	but checkout side &&

	but rm --cached frotz/filfre &&
	mv frotz xyzzy &&
	test_ln_s_add xyzzy frotz &&
	but add xyzzy/filfre &&
	test_tick &&
	but cummit -m "side moves frotz/ to xyzzy/ and adds frotz->xyzzy/"

'

test_expect_success 'switch from symlink to dir' '

	but checkout main

'

test_expect_success 'Remove temporary directories & switch to main' '
	rm -fr frotz xyzzy nitfol &&
	but checkout -f main
'

test_expect_success 'switch from dir to symlink' '

	but checkout side

'

test_done
