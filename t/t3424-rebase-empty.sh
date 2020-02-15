#!/bin/sh

test_description='git rebase of commits that start or become empty'

. ./test-lib.sh

test_expect_success 'setup test repository' '
	test_write_lines 1 2 3 4 5 6 7 8 9 10 >numbers &&
	test_write_lines A B C D E F G H I J >letters &&
	git add numbers letters &&
	git commit -m A &&

	git branch upstream &&
	git branch localmods &&

	git checkout upstream &&
	test_write_lines A B C D E >letters &&
	git add letters &&
	git commit -m B &&

	test_write_lines 1 2 3 4 five 6 7 8 9 ten >numbers &&
	git add numbers &&
	git commit -m C &&

	git checkout localmods &&
	test_write_lines 1 2 3 4 five 6 7 8 9 10 >numbers &&
	git add numbers &&
	git commit -m C2 &&

	git commit --allow-empty -m D &&

	test_write_lines A B C D E >letters &&
	git add letters &&
	git commit -m "Five letters ought to be enough for anybody"
'

test_expect_failure 'rebase (am-backend) with a variety of empty commits' '
	test_when_finished "git rebase --abort" &&
	git checkout -B testing localmods &&
	# rebase (--am) should not drop commits that start empty
	git rebase upstream &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_failure 'rebase --merge with a variety of empty commits' '
	test_when_finished "git rebase --abort" &&
	git checkout -B testing localmods &&
	# rebase --merge should not halt on the commit that becomes empty
	git rebase --merge upstream &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --interactive with a variety of empty commits' '
	git checkout -B testing localmods &&
	test_must_fail git rebase --interactive upstream &&

	git rebase --skip &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_done
