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
	git replay --output-commands --onto main topic1..topic2 >result &&

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
	# Create a test branch that wont interfere with others
	git branch atomic-test topic2 &&
	git rev-parse atomic-test >atomic-test-old &&

	# Default behavior: atomic ref updates (no output)
	git replay --onto main topic1..atomic-test >output &&
	test_must_be_empty output &&

	# Verify the branch was updated
	git rev-parse atomic-test >atomic-test-new &&
	! test_cmp atomic-test-old atomic-test-new &&

	# Verify the history is correct
	git log --format=%s atomic-test >actual &&
	test_write_lines E D M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'using replay on bare repo to rebase two branches, one on top of other' '
	git -C bare replay --output-commands --onto main topic1..topic2 >result-bare &&

	# The result should match what we got from the regular repo
	test_cmp result result-bare
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

	git replay --output-commands --advance main topic1..topic2 >result &&

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
	git -C bare replay --output-commands --advance main topic1..topic2 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'replay on bare repo fails with both --advance and --onto' '
	test_must_fail git -C bare replay --advance main --onto main topic1..topic2 >result-bare
'

test_expect_success 'replay fails when both --advance and --onto are omitted' '
	test_must_fail git replay topic1..topic2 >result
'

test_expect_success 'using replay to also rebase a contained branch' '
	git replay --output-commands --contained --onto main main..topic3 >result &&

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
	git -C bare replay --output-commands --contained --onto main main..topic3 >result-bare &&
	test_cmp expect result-bare
'

test_expect_success 'using replay to rebase multiple divergent branches' '
	git replay --output-commands --onto main ^topic1 topic2 topic4 >result &&

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
	git -C bare replay --output-commands --contained --onto main ^main topic2 topic3 topic4 >result &&

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

# Tests for new default atomic behavior and options

test_expect_success 'replay default behavior should not produce output when successful' '
	git replay --onto main topic1..topic3 >output &&
	test_must_be_empty output
'

test_expect_success 'replay with --output-commands produces traditional output' '
	git replay --output-commands --onto main topic1..topic3 >output &&
	test_line_count = 1 output &&
	grep "^update refs/heads/topic3 " output
'

test_expect_success 'replay with --allow-partial should not produce output when successful' '
	git replay --allow-partial --onto main topic1..topic3 >output &&
	test_must_be_empty output
'

test_expect_success 'replay fails when --output-commands and --allow-partial are used together' '
	test_must_fail git replay --output-commands --allow-partial --onto main topic1..topic2 2>error &&
	grep "cannot be used together" error
'

test_expect_success 'replay with --contained updates multiple branches atomically' '
	# Create fresh test branches based on the original structure
	# contained-topic1 should be contained within the range to contained-topic3
	git branch contained-base main &&
	git checkout -b contained-topic1 contained-base &&
	test_commit ContainedC &&
	git checkout -b contained-topic3 contained-topic1 &&
	test_commit ContainedG &&
	test_commit ContainedH &&
	git checkout main &&

	# Store original states
	git rev-parse contained-topic1 >contained-topic1-old &&
	git rev-parse contained-topic3 >contained-topic3-old &&

	# Use --contained to update multiple branches - this should update both
	git replay --contained --onto main contained-base..contained-topic3 &&

	# Verify both branches were updated
	git rev-parse contained-topic1 >contained-topic1-new &&
	git rev-parse contained-topic3 >contained-topic3-new &&
	! test_cmp contained-topic1-old contained-topic1-new &&
	! test_cmp contained-topic3-old contained-topic3-new
'

test_expect_success 'replay atomic behavior: all refs updated or none' '
	# Store original state
	git rev-parse topic4 >topic4-old &&

	# Default atomic behavior
	git replay --onto main main..topic4 &&

	# Verify ref was updated
	git rev-parse topic4 >topic4-new &&
	! test_cmp topic4-old topic4-new &&

	# Verify no partial state
	git log --format=%s topic4 >actual &&
	test_write_lines J I M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'replay works correctly with bare repositories' '
	# Test atomic behavior in bare repo (important for Gitaly)
	git checkout -b bare-test topic1 &&
	test_commit BareTest &&

	# Test with bare repo - replay the commits from main..bare-test to get the full history
	git -C bare fetch .. bare-test:bare-test &&
	git -C bare replay --onto main main..bare-test &&

	# Verify the bare repo was updated correctly (no output)
	git -C bare log --format=%s bare-test >actual &&
	test_write_lines BareTest F C M L B A >expect &&
	test_cmp expect actual
'

test_expect_success 'replay --allow-partial with no failures produces no output' '
	git checkout -b partial-test topic1 &&
	test_commit PartialTest &&

	# Should succeed silently even with partial mode
	git replay --allow-partial --onto main topic1..partial-test >output &&
	test_must_be_empty output
'

test_expect_success 'replay maintains ref update consistency' '
	# Test that traditional vs atomic produce equivalent results
	git checkout -b method1-test topic2 &&
	git checkout -b method2-test topic2 &&

	# Both methods should update refs to point to the same replayed commits
	git replay --output-commands --onto main topic1..method1-test >update-commands &&
	git update-ref --stdin <update-commands &&
	git log --format=%s method1-test >traditional-result &&

	# Direct atomic method should produce same commit history
	git replay --onto main topic1..method2-test &&
	git log --format=%s method2-test >atomic-result &&

	# Both methods should produce identical commit histories
	test_cmp traditional-result atomic-result
'

test_expect_success 'replay error messages are helpful and clear' '
	# Test that error messages are clear
	test_must_fail git replay --output-commands --allow-partial --onto main topic1..topic2 2>error &&
	grep "cannot be used together" error
'

test_expect_success 'replay with empty range produces no output and no changes' '
	# Create a test branch for empty range testing
	git checkout -b empty-test topic1 &&
	git rev-parse empty-test >empty-test-before &&

	# Empty range should succeed but do nothing
	git replay --onto main empty-test..empty-test >output &&
	test_must_be_empty output &&

	# Branch should be unchanged
	git rev-parse empty-test >empty-test-after &&
	test_cmp empty-test-before empty-test-after
'

test_done
