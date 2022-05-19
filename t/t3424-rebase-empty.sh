#!/bin/sh

test_description='but rebase of cummits that start or become empty'

. ./test-lib.sh

test_expect_success 'setup test repository' '
	test_write_lines 1 2 3 4 5 6 7 8 9 10 >numbers &&
	test_write_lines A B C D E F G H I J >letters &&
	but add numbers letters &&
	but cummit -m A &&

	but branch upstream &&
	but branch localmods &&

	but checkout upstream &&
	test_write_lines A B C D E >letters &&
	but add letters &&
	but cummit -m B &&

	test_write_lines 1 2 3 4 five 6 7 8 9 ten >numbers &&
	but add numbers &&
	but cummit -m C &&

	but checkout localmods &&
	test_write_lines 1 2 3 4 five 6 7 8 9 10 >numbers &&
	but add numbers &&
	but cummit -m C2 &&

	but cummit --allow-empty -m D &&

	test_write_lines A B C D E >letters &&
	but add letters &&
	but cummit -m "Five letters ought to be enough for anybody"
'

test_expect_failure 'rebase (apply-backend)' '
	test_when_finished "but rebase --abort" &&
	but checkout -B testing localmods &&
	# rebase (--apply) should not drop cummits that start empty
	but rebase --apply upstream &&

	test_write_lines D C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=drop' '
	but checkout -B testing localmods &&
	but rebase --merge --empty=drop upstream &&

	test_write_lines D C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge uses default of --empty=drop' '
	but checkout -B testing localmods &&
	but rebase --merge upstream &&

	test_write_lines D C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=keep' '
	but checkout -B testing localmods &&
	but rebase --merge --empty=keep upstream &&

	test_write_lines D C2 C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=ask' '
	but checkout -B testing localmods &&
	test_must_fail but rebase --merge --empty=ask upstream &&

	but rebase --skip &&

	test_write_lines D C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --interactive --empty=drop' '
	but checkout -B testing localmods &&
	but rebase --interactive --empty=drop upstream &&

	test_write_lines D C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --interactive --empty=keep' '
	but checkout -B testing localmods &&
	but rebase --interactive --empty=keep upstream &&

	test_write_lines D C2 C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --interactive --empty=ask' '
	but checkout -B testing localmods &&
	test_must_fail but rebase --interactive --empty=ask upstream &&

	but rebase --skip &&

	test_write_lines D C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --interactive uses default of --empty=ask' '
	but checkout -B testing localmods &&
	test_must_fail but rebase --interactive upstream &&

	but rebase --skip &&

	test_write_lines D C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=drop --keep-empty' '
	but checkout -B testing localmods &&
	but rebase --merge --empty=drop --keep-empty upstream &&

	test_write_lines D C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=drop --no-keep-empty' '
	but checkout -B testing localmods &&
	but rebase --merge --empty=drop --no-keep-empty upstream &&

	test_write_lines C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=keep --keep-empty' '
	but checkout -B testing localmods &&
	but rebase --merge --empty=keep --keep-empty upstream &&

	test_write_lines D C2 C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge --empty=keep --no-keep-empty' '
	but checkout -B testing localmods &&
	but rebase --merge --empty=keep --no-keep-empty upstream &&

	test_write_lines C2 C B A >expect &&
	but log --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase --merge does not leave state laying around' '
	but checkout -B testing localmods~2 &&
	but rebase --merge upstream &&

	test_path_is_missing .but/CHERRY_PICK_HEAD &&
	test_path_is_missing .but/MERGE_MSG
'

test_done
