#!/bin/sh

test_description='git rebase across mode change'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir DS &&
	>DS/whatever &&
	git add DS &&
	git commit -m base &&

	git branch side1 &&
	git branch side2 &&

	git checkout side1 &&
	git rm -rf DS &&
	test_ln_s_add unrelated DS &&
	git commit -m side1 &&

	git checkout side2 &&
	>unrelated &&
	git add unrelated &&
	git commit -m commit1 &&

	echo >>unrelated &&
	git commit -am commit2
'

test_expect_success 'rebase changes with the apply backend' '
	test_when_finished "git rebase --abort || true" &&
	git checkout -b apply-backend side2 &&
	git rebase side1
'

test_expect_success 'rebase changes with the merge backend' '
	test_when_finished "git rebase --abort || true" &&
	git checkout -b merge-backend side2 &&
	git rebase -m side1
'

test_expect_success 'rebase changes with the merge backend with a delay' '
	test_when_finished "git rebase --abort || true" &&
	git checkout -b merge-delay-backend side2 &&
	git rebase -m --exec "sleep 1" side1
'

test_done
