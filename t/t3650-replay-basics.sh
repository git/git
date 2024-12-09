#!/bin/sh

test_description='basic git replay tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

GIT_AUTHOR_NAME=author@name
GIT_AUTHOR_EMAIL=bogus@email@address
export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL

test_expect_success 'setup' '
	test_commit A &&
	test_commit B &&

	git switch -c topic1 &&
	test_commit C &&
	git switch -c topic2 &&
	test_commit D &&
	test_commit E &&
	git switch topic1 &&
	test_commit F &&
	git switch -c topic3 &&
	test_commit G &&
	test_commit H &&
	git switch -c topic4 main &&
	test_commit I &&
	test_commit J &&

	git switch -c next main &&
	test_commit K &&
	git merge -m "Merge topic1" topic1 &&
	git merge -m "Merge topic2" topic2 &&
	git merge -m "Merge topic3" topic3 &&
	>evil &&
	git add evil &&
	git commit --amend &&
	git merge -m "Merge topic4" topic4 &&

	git switch main &&
	test_commit L &&
	test_commit M &&

	git switch -c conflict B &&
	test_commit C.conflict C.t conflict
'

test_expect_success 'setup bare' '
	git clone --bare . bare
'

test_expect_success 'using replay to rebase two branches, one on top of other' '
	git replay --onto main topic1..topic2 >result &&

	test_line_count = 1 result &&

	git log --format=%s $(cut -f 3 -d " " result) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic2 " >expect &&
	printf "%s " $(cut -f 3 -d " " result) >>expect &&
	git rev-parse topic2 >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay on bare repo to rebase two branches, one on top of other' '
	git -C bare replay --onto main topic1..topic2 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'using replay to rebase with a conflict' '
	test_expect_code 1 git replay --onto topic1 B..conflict
'

test_expect_success 'using replay on bare repo to rebase with a conflict' '
	test_expect_code 1 git -C bare replay --onto topic1 B..conflict
'

test_expect_success 'using replay to perform basic cherry-pick' '
	# The differences between this test and previous ones are:
	#   --advance vs --onto
	# 2nd field of result is refs/heads/main vs. refs/heads/topic2
	# 4th field of result is hash for main instead of hash for topic2

	git replay --advance main topic1..topic2 >result &&

	test_line_count = 1 result &&

	git log --format=%s $(cut -f 3 -d " " result) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/main " >expect &&
	printf "%s " $(cut -f 3 -d " " result) >>expect &&
	git rev-parse main >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay on bare repo to perform basic cherry-pick' '
	git -C bare replay --advance main topic1..topic2 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'replay on bare repo fails with both --advance and --onto' '
	test_must_fail git -C bare replay --advance main --onto main topic1..topic2 >result-bare
'

test_expect_success 'replay fails when both --advance and --onto are omitted' '
	test_must_fail git replay topic1..topic2 >result
'

test_expect_success 'using replay to also rebase a contained branch' '
	git replay --contained --onto main main..topic3 >result &&

	test_line_count = 2 result &&
	cut -f 3 -d " " result >new-branch-tips &&

	git log --format=%s $(head -n 1 new-branch-tips) >actual &&
	test_write_lines F C M L B A >expect &&
	test_cmp expect actual &&

	git log --format=%s $(tail -n 1 new-branch-tips) >actual &&
	test_write_lines H G F C M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic1 " >expect &&
	printf "%s " $(head -n 1 new-branch-tips) >>expect &&
	git rev-parse topic1 >>expect &&
	printf "update refs/heads/topic3 " >>expect &&
	printf "%s " $(tail -n 1 new-branch-tips) >>expect &&
	git rev-parse topic3 >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay on bare repo to also rebase a contained branch' '
	git -C bare replay --contained --onto main main..topic3 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'using replay to rebase multiple divergent branches' '
	git replay --onto main ^topic1 topic2 topic4 >result &&

	test_line_count = 2 result &&
	cut -f 3 -d " " result >new-branch-tips &&

	git log --format=%s $(head -n 1 new-branch-tips) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	git log --format=%s $(tail -n 1 new-branch-tips) >actual &&
	test_write_lines J I M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic2 " >expect &&
	printf "%s " $(head -n 1 new-branch-tips) >>expect &&
	git rev-parse topic2 >>expect &&
	printf "update refs/heads/topic4 " >>expect &&
	printf "%s " $(tail -n 1 new-branch-tips) >>expect &&
	git rev-parse topic4 >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay on bare repo to rebase multiple divergent branches, including contained ones' '
	git -C bare replay --contained --onto main ^main topic2 topic3 topic4 >result &&

	test_line_count = 4 result &&
	cut -f 3 -d " " result >new-branch-tips &&

	>expect &&
	for i in 2 1 3 4
	do
		printf "update refs/heads/topic$i " >>expect &&
		printf "%s " $(grep topic$i result | cut -f 3 -d " ") >>expect &&
		git -C bare rev-parse topic$i >>expect || return 1
	done &&

	test_cmp expect result &&

	test_write_lines F C M L B A >expect1 &&
	test_write_lines E D C M L B A >expect2 &&
	test_write_lines H G F C M L B A >expect3 &&
	test_write_lines J I M L B A >expect4 &&

	for i in 1 2 3 4
	do
		git -C bare log --format=%s $(grep topic$i result | cut -f 3 -d " ") >actual &&
		test_cmp expect$i actual || return 1
	done
'

test_done
