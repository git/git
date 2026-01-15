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

	git switch --detach topic4 &&
	test_commit N &&
	test_commit O &&
	git switch -c topic-with-merge topic4 &&
	test_merge P O --no-ff &&
	git switch main &&

	git switch -c conflict B &&
	test_commit C.conflict C.t conflict
'

test_expect_success 'setup bare' '
	git clone --bare . bare
'

test_expect_success 'argument to --advance must be a reference' '
	echo "fatal: argument to --advance must be a reference" >expect &&
	oid=$(git rev-parse main) &&
	test_must_fail git replay --advance=$oid topic1..topic2 2>actual &&
	test_cmp expect actual
'

test_expect_success '--onto with invalid commit-ish' '
	printf "fatal: ${SQ}refs/not-valid${SQ} is not " >expect &&
	printf "a valid commit-ish for --onto\n" >>expect &&
	test_must_fail git replay --onto=refs/not-valid topic1..topic2 2>actual &&
	test_cmp expect actual
'

test_expect_success 'option --onto or --advance is mandatory' '
	echo "error: option --onto or --advance is mandatory" >expect &&
	test_might_fail git replay -h >>expect &&
	test_must_fail git replay topic1..topic2 2>actual &&
	test_cmp expect actual
'

test_expect_success 'no base or negative ref gives no-replaying down to root error' '
	echo "fatal: replaying down from root commit is not supported yet!" >expect &&
	test_must_fail git replay --onto=topic1 topic2 2>actual &&
	test_cmp expect actual
'

test_expect_success 'options --advance and --contained cannot be used together' '
	printf "fatal: options ${SQ}--advance${SQ} " >expect &&
	printf "and ${SQ}--contained${SQ} cannot be used together\n" >>expect &&
	test_must_fail git replay --advance=main --contained \
		topic1..topic2 2>actual &&
	test_cmp expect actual
'

test_expect_success 'cannot advance target ... ordering would be ill-defined' '
	echo "fatal: cannot advance target with multiple sources because ordering would be ill-defined" >expect &&
	test_must_fail git replay --advance=main main topic1 topic2 2>actual &&
	test_cmp expect actual
'

test_expect_success 'replaying merge commits is not supported yet' '
	echo "fatal: replaying merge commits is not supported yet!" >expect &&
	test_must_fail git replay --advance=main main..topic-with-merge 2>actual &&
	test_cmp expect actual
'

test_expect_success 'using replay to rebase two branches, one on top of other' '
	git replay --ref-action=print --onto main topic1..topic2 >result &&

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
	git -C bare replay --ref-action=print --onto main topic1..topic2 >result-bare &&
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

	git replay --ref-action=print --advance main topic1..topic2 >result &&

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
	git -C bare replay --ref-action=print --advance main topic1..topic2 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'replay on bare repo fails with both --advance and --onto' '
	test_must_fail git -C bare replay --advance main --onto main topic1..topic2 >result-bare
'

test_expect_success 'replay fails when both --advance and --onto are omitted' '
	test_must_fail git replay topic1..topic2 >result
'

test_expect_success 'using replay to also rebase a contained branch' '
	git replay --ref-action=print --contained --onto main main..topic3 >result &&

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
	git -C bare replay --ref-action=print --contained --onto main main..topic3 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'using replay to rebase multiple divergent branches' '
	git replay --ref-action=print --onto main ^topic1 topic2 topic4 >result &&

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
	git -C bare replay --ref-action=print --contained --onto main ^main topic2 topic3 topic4 >result &&

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

test_expect_success 'merge.directoryRenames=false' '
	# create a test case that stress-tests the rename caching
	git switch -c rename-onto &&

	mkdir -p to-rename &&
	test_commit to-rename/move &&

	mkdir -p renamed-directory &&
	git mv to-rename/move* renamed-directory/ &&
	test_tick &&
	git commit -m renamed-directory &&

	git switch -c rename-from HEAD^ &&
	test_commit to-rename/add-a-file &&
	echo modified >to-rename/add-a-file.t &&
	test_tick &&
	git commit -m modified to-rename/add-a-file.t &&

	git -c merge.directoryRenames=false replay \
		--onto rename-onto rename-onto..rename-from
'

test_expect_success 'default atomic behavior updates refs directly' '
	# Use a separate branch to avoid contaminating topic2 for later tests
	git branch test-atomic topic2 &&
	test_when_finished "git branch -D test-atomic" &&

	# Test default atomic behavior (no output, refs updated)
	git replay --onto main topic1..test-atomic >output &&
	test_must_be_empty output &&

	# Verify ref was updated
	git log --format=%s test-atomic >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	# Verify reflog message includes SHA of onto commit
	git reflog test-atomic -1 --format=%gs >reflog-msg &&
	ONTO_SHA=$(git rev-parse main) &&
	echo "replay --onto $ONTO_SHA" >expect-reflog &&
	test_cmp expect-reflog reflog-msg
'

test_expect_success 'atomic behavior in bare repository' '
	# Store original state for cleanup
	START=$(git -C bare rev-parse topic2) &&
	test_when_finished "git -C bare update-ref refs/heads/topic2 $START" &&

	# Test atomic updates work in bare repo
	git -C bare replay --onto main topic1..topic2 >output &&
	test_must_be_empty output &&

	# Verify ref was updated in bare repo
	git -C bare log --format=%s topic2 >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'reflog message for --advance mode' '
	# Store original state
	START=$(git rev-parse main) &&
	test_when_finished "git update-ref refs/heads/main $START" &&

	# Test --advance mode reflog message
	git replay --advance main topic1..topic2 >output &&
	test_must_be_empty output &&

	# Verify reflog message includes --advance and branch name
	git reflog main -1 --format=%gs >reflog-msg &&
	echo "replay --advance main" >expect-reflog &&
	test_cmp expect-reflog reflog-msg
'

test_expect_success 'replay.refAction=print config option' '
	# Store original state
	START=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START" &&

	# Test with config set to print
	test_config replay.refAction print &&
	git replay --onto main topic1..topic2 >output &&
	test_line_count = 1 output &&
	test_grep "^update refs/heads/topic2 " output
'

test_expect_success 'replay.refAction=update config option' '
	# Store original state
	START=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START" &&

	# Test with config set to update
	test_config replay.refAction update &&
	git replay --onto main topic1..topic2 >output &&
	test_must_be_empty output &&

	# Verify ref was updated
	git log --format=%s topic2 >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'command-line --ref-action overrides config' '
	# Store original state
	START=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START" &&

	# Set config to update but use --ref-action=print
	test_config replay.refAction update &&
	git replay --ref-action=print --onto main topic1..topic2 >output &&
	test_line_count = 1 output &&
	test_grep "^update refs/heads/topic2 " output
'

test_expect_success 'invalid replay.refAction value' '
	test_config replay.refAction invalid &&
	test_must_fail git replay --onto main topic1..topic2 2>error &&
	test_grep "invalid.*replay.refAction.*value" error
'

test_done
