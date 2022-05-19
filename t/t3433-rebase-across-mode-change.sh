#!/bin/sh

test_description='but rebase across mode change'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir DS &&
	>DS/whatever &&
	but add DS &&
	but cummit -m base &&

	but branch side1 &&
	but branch side2 &&

	but checkout side1 &&
	but rm -rf DS &&
	test_ln_s_add unrelated DS &&
	but cummit -m side1 &&

	but checkout side2 &&
	>unrelated &&
	but add unrelated &&
	but cummit -m cummit1 &&

	echo >>unrelated &&
	but cummit -am cummit2
'

test_expect_success 'rebase changes with the apply backend' '
	test_when_finished "but rebase --abort || true" &&
	but checkout -b apply-backend side2 &&
	but rebase side1
'

test_expect_success 'rebase changes with the merge backend' '
	test_when_finished "but rebase --abort || true" &&
	but checkout -b merge-backend side2 &&
	but rebase -m side1
'

test_expect_success 'rebase changes with the merge backend with a delay' '
	test_when_finished "but rebase --abort || true" &&
	but checkout -b merge-delay-backend side2 &&
	but rebase -m --exec "sleep 1" side1
'

test_done
