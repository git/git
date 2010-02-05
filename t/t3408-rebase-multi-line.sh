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

But otherwise with a sane description."

	git branch side &&

	git reset --hard HEAD^ &&
	>elif &&
	git add elif &&
	test_tick &&
	git commit -m second

'

test_expect_success rebase '

	git checkout side &&
	git rebase master &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	git cat-file commit side@{1} | sed -e "1,/^\$/d" >expect &&
	test_cmp expect actual

'

test_done
