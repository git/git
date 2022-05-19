#!/bin/sh

test_description='rebasing a cummit with multi-line first paragraph.'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	>file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&

	echo hello >file &&
	test_tick &&
	but cummit -a -m "A sample cummit log message that has a long
summary that spills over multiple lines.

But otherwise with a sane description." &&

	but branch side &&

	but reset --hard HEAD^ &&
	>elif &&
	but add elif &&
	test_tick &&
	but cummit -m second &&

	but checkout -b side2 &&
	>afile &&
	but add afile &&
	test_tick &&
	but cummit -m third &&
	echo hello >afile &&
	test_tick &&
	but cummit -a -m fourth &&
	but checkout -b side-merge &&
	but reset --hard HEAD^^ &&
	but merge --no-ff -m "A merge cummit log message that has a long
summary that spills over multiple lines.

But otherwise with a sane description." side2 &&
	but branch side-merge-original
'

test_expect_success rebase '

	but checkout side &&
	but rebase main &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	but cat-file cummit side@{1} | sed -e "1,/^\$/d" >expect &&
	test_cmp expect actual

'
test_done
