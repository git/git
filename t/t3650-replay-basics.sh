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
	git replay --update-refs=print --onto main topic1..topic2 >result &&

	test_line_count = 1 result &&

	git log --format=%s $(cut -f 3 -d " " result) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic2 " >expect &&
	printf "%s " $(cut -f 3 -d " " result) >>expect &&
	git rev-parse topic2 >>expect &&

	test_cmp expect result
'

test_expect_success 'using replay with default atomic behavior (no output)' '
	# Store the original state
	START=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START" &&

	# Default behavior: atomic ref updates (no output)
	git replay --onto main topic1..topic2 >output &&
	test_must_be_empty output &&

	# Verify the history is correct
	git log --format=%s topic2 >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'using replay on bare repo to rebase two branches, one on top of other' '
	git -C bare replay --update-refs=print --onto main topic1..topic2 >result-bare &&

	test_line_count = 1 result-bare &&

	git log --format=%s $(cut -f 3 -d " " result-bare) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic2 " >expect &&
	printf "%s " $(cut -f 3 -d " " result-bare) >>expect &&
	git -C bare rev-parse topic2 >>expect &&

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

	git replay --update-refs=print --advance main topic1..topic2 >result &&

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
	git -C bare replay --update-refs=print --advance main topic1..topic2 >result-bare &&

	test_line_count = 1 result-bare &&

	git log --format=%s $(cut -f 3 -d " " result-bare) >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/main " >expect &&
	printf "%s " $(cut -f 3 -d " " result-bare) >>expect &&
	git -C bare rev-parse main >>expect &&

	test_cmp expect result-bare
'

test_expect_success 'replay on bare repo fails with both --advance and --onto' '
	test_must_fail git -C bare replay --advance main --onto main topic1..topic2 >result-bare
'

test_expect_success 'replay fails when both --advance and --onto are omitted' '
	test_must_fail git replay topic1..topic2 >result
'

test_expect_success 'using replay to also rebase a contained branch' '
	git replay --update-refs=print --contained --onto main main..topic3 >result &&

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
	git -C bare replay --update-refs=print --contained --onto main main..topic3 >result-bare &&

	test_line_count = 2 result-bare &&
	cut -f 3 -d " " result-bare >new-branch-tips &&

	git log --format=%s $(head -n 1 new-branch-tips) >actual &&
	test_write_lines F C M L B A >expect &&
	test_cmp expect actual &&

	git log --format=%s $(tail -n 1 new-branch-tips) >actual &&
	test_write_lines H G F C M L B A >expect &&
	test_cmp expect actual &&

	printf "update refs/heads/topic1 " >expect &&
	printf "%s " $(head -n 1 new-branch-tips) >>expect &&
	git -C bare rev-parse topic1 >>expect &&
	printf "update refs/heads/topic3 " >>expect &&
	printf "%s " $(tail -n 1 new-branch-tips) >>expect &&
	git -C bare rev-parse topic3 >>expect &&

	test_cmp expect result-bare
'

test_expect_success 'using replay to rebase multiple divergent branches' '
	git replay --update-refs=print --onto main ^topic1 topic2 topic4 >result &&

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
	git -C bare replay --update-refs=print --contained --onto main ^main topic2 topic3 topic4 >result &&

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

# Tests for atomic ref update behavior

test_expect_success 'replay with --contained updates multiple branches atomically' '
	# Store original states
	START_TOPIC1=$(git rev-parse topic1) &&
	START_TOPIC3=$(git rev-parse topic3) &&
	test_when_finished "git branch -f topic1 $START_TOPIC1 && git branch -f topic3 $START_TOPIC3" &&

	# Use --contained to update multiple branches
	git replay --contained --onto main main..topic3 >output &&
	test_must_be_empty output &&

	# Verify both branches were updated with correct commit sequences
	git log --format=%s topic1 >actual &&
	test_write_lines F C M L B A >expect &&
	test_cmp expect actual &&

	git log --format=%s topic3 >actual &&
	test_write_lines H G F C M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'replay atomic guarantee: all refs updated or none' '
	# Store original states
	START_TOPIC1=$(git rev-parse topic1) &&
	START_TOPIC3=$(git rev-parse topic3) &&
	test_when_finished "git branch -f topic1 $START_TOPIC1 && git branch -f topic3 $START_TOPIC3" &&

	# Create a lock on topic1 to simulate a concurrent update
	>.git/refs/heads/topic1.lock &&

	# Try to update multiple branches with --contained
	# This should fail atomically - neither branch should be updated
	test_must_fail git replay --contained --onto main main..topic3 2>error &&

	# Remove the lock before checking refs
	rm -f .git/refs/heads/topic1.lock &&

	# Verify the transaction failed
	grep "failed to commit ref transaction" error &&

	# Verify NEITHER branch was updated (all-or-nothing guarantee)
	test_cmp_rev $START_TOPIC1 topic1 &&
	test_cmp_rev $START_TOPIC3 topic3
'

test_expect_success 'traditional pipeline and atomic update produce equivalent results' '
	# Store original states
	START_TOPIC2=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START_TOPIC2" &&

	# Traditional method: output commands and pipe to update-ref
	git replay --update-refs=print --onto main topic1..topic2 >update-commands &&
	git update-ref --stdin <update-commands &&
	git log --format=%s topic2 >traditional-result &&

	# Reset topic2
	git branch -f topic2 $START_TOPIC2 &&

	# Atomic method: direct ref updates
	git replay --onto main topic1..topic2 &&
	git log --format=%s topic2 >atomic-result &&

	# Both methods should produce identical commit histories
	test_cmp traditional-result atomic-result
'

test_expect_success 'replay works correctly with bare repositories' '
	# Test atomic behavior in bare repo
	git -C bare fetch .. topic1:bare-test-branch &&
	git -C bare replay --onto main main..bare-test-branch >output &&
	test_must_be_empty output &&

	# Verify the bare repo was updated correctly
	git -C bare log --format=%s bare-test-branch >actual &&
	test_write_lines F C M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'replay validates --update-refs mode values' '
	test_must_fail git replay --update-refs=invalid --onto main topic1..topic2 2>error &&
	grep "invalid value for --update-refs" error
'

test_expect_success 'replay.defaultAction config option' '
	# Store original state
	START=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START && git config --unset replay.defaultAction" &&

	# Set config to show-commands
	git config replay.defaultAction show-commands &&
	git replay --onto main topic1..topic2 >output &&
	test_line_count = 1 output &&
	grep "^update refs/heads/topic2 " output &&

	# Reset and test update-refs mode
	git branch -f topic2 $START &&
	git config replay.defaultAction update-refs &&
	git replay --onto main topic1..topic2 >output &&
	test_must_be_empty output &&

	# Verify ref was updated
	git log --format=%s topic2 >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'command-line --update-refs overrides config' '
	# Store original state
	START=$(git rev-parse topic2) &&
	test_when_finished "git branch -f topic2 $START && git config --unset replay.defaultAction" &&

	# Set config to update-refs but use --update-refs=print
	git config replay.defaultAction update-refs &&
	git replay --update-refs=print --onto main topic1..topic2 >output &&
	test_line_count = 1 output &&
	grep "^update refs/heads/topic2 " output
'

test_expect_success 'invalid replay.defaultAction value' '
	test_when_finished "git config --unset replay.defaultAction" &&
	git config replay.defaultAction invalid &&
	test_must_fail git replay --onto main topic1..topic2 2>error &&
	grep "invalid value for replay.defaultAction" error
'

test_done
