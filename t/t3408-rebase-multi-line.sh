#!/bin/sh

test_description='rebasing a commit with multi-line first paragraph.'

. ./test-lib.sh

test_expect_success setup '

	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&

	echo hello >file &&
	test_tick &&
	git commit -a -m "A sample commit log message that has a long
summary that spills over multiple lines.

But otherwise with a sane description." &&

	git branch side &&

	git reset --hard HEAD^ &&
	>elif &&
	git add elif &&
	test_tick &&
	git commit -m second &&

	git checkout -b side2 &&
	>afile &&
	git add afile &&
	test_tick &&
	git commit -m third &&
	echo hello >afile &&
	test_tick &&
	git commit -a -m fourth &&
	git checkout -b side-merge &&
	git reset --hard HEAD^^ &&
	git merge --no-ff -m "A merge commit log message that has a long
summary that spills over multiple lines.

But otherwise with a sane description." side2 &&
	git branch side-merge-original
'

test_expect_success rebase '

	git checkout side &&
	git rebase master &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	git cat-file commit side@{1} | sed -e "1,/^\$/d" >expect &&
	test_cmp expect actual

'
test_expect_success REBASE_P rebasep '

	git checkout side-merge &&
	git rebase -p side &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	git cat-file commit side-merge-original | sed -e "1,/^\$/d" >expect &&
	test_cmp expect actual

'

test_done
