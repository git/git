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

test_done
