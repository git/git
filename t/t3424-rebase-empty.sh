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

test_expect_failure 'rebase (apply-backend)' '
	test_when_finished "git rebase --abort" &&
	git checkout -B testing localmods &&
	# rebase (--apply) should not drop commits that start empty
	git rebase --apply upstream &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=drop' '
	git checkout -B testing localmods &&
	git rebase --merge --empty=drop upstream &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge uses default of --empty=drop' '
	git checkout -B testing localmods &&
	git rebase --merge upstream &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=keep' '
	git checkout -B testing localmods &&
	git rebase --merge --empty=keep upstream &&

	test_write_lines D C2 C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=ask' '
	git checkout -B testing localmods &&
	test_must_fail git rebase --merge --empty=ask upstream &&

	git rebase --skip &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --interactive --empty=drop' '
	git checkout -B testing localmods &&
	git rebase --interactive --empty=drop upstream &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --interactive --empty=keep' '
	git checkout -B testing localmods &&
	git rebase --interactive --empty=keep upstream &&

	test_write_lines D C2 C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --interactive --empty=ask' '
	git checkout -B testing localmods &&
	test_must_fail git rebase --interactive --empty=ask upstream &&

	git rebase --skip &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --interactive uses default of --empty=ask' '
	git checkout -B testing localmods &&
	test_must_fail git rebase --interactive upstream &&

	git rebase --skip &&

	test_write_lines D C B A >expect &&
	git log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge does not leave state laying around' '
	git checkout -B testing localmods~2 &&
	git rebase --merge upstream &&

	test_path_is_missing .git/CHERRY_PICK_HEAD &&
	test_path_is_missing .git/MERGE_MSG
'

test_done
