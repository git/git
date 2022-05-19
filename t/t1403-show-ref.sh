#!/bin/sh

test_description='show-ref'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	test_cummit --annotate A &&
	but checkout -b side &&
	test_cummit --annotate B &&
	but checkout main &&
	test_cummit C &&
	but branch B A^0
'

test_expect_success 'show-ref' '
	echo $(but rev-parse refs/tags/A) refs/tags/A >expect &&

	but show-ref A >actual &&
	test_cmp expect actual &&

	but show-ref tags/A >actual &&
	test_cmp expect actual &&

	but show-ref refs/tags/A >actual &&
	test_cmp expect actual &&

	test_must_fail but show-ref D >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref -q' '
	but show-ref -q A >actual &&
	test_must_be_empty actual &&

	but show-ref -q tags/A >actual &&
	test_must_be_empty actual &&

	but show-ref -q refs/tags/A >actual &&
	test_must_be_empty actual &&

	test_must_fail but show-ref -q D >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref --verify' '
	echo $(but rev-parse refs/tags/A) refs/tags/A >expect &&

	but show-ref --verify refs/tags/A >actual &&
	test_cmp expect actual &&

	test_must_fail but show-ref --verify A >actual &&
	test_must_be_empty actual &&

	test_must_fail but show-ref --verify tags/A >actual &&
	test_must_be_empty actual &&

	test_must_fail but show-ref --verify D >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref --verify -q' '
	but show-ref --verify -q refs/tags/A >actual &&
	test_must_be_empty actual &&

	test_must_fail but show-ref --verify -q A >actual &&
	test_must_be_empty actual &&

	test_must_fail but show-ref --verify -q tags/A >actual &&
	test_must_be_empty actual &&

	test_must_fail but show-ref --verify -q D >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref -d' '
	{
		echo $(but rev-parse refs/tags/A) refs/tags/A &&
		echo $(but rev-parse refs/tags/A^0) "refs/tags/A^{}" &&
		echo $(but rev-parse refs/tags/C) refs/tags/C
	} >expect &&
	but show-ref -d A C >actual &&
	test_cmp expect actual &&

	but show-ref -d tags/A tags/C >actual &&
	test_cmp expect actual &&

	but show-ref -d refs/tags/A refs/tags/C >actual &&
	test_cmp expect actual &&

	but show-ref --verify -d refs/tags/A refs/tags/C >actual &&
	test_cmp expect actual &&

	echo $(but rev-parse refs/heads/main) refs/heads/main >expect &&
	but show-ref -d main >actual &&
	test_cmp expect actual &&

	but show-ref -d heads/main >actual &&
	test_cmp expect actual &&

	but show-ref -d refs/heads/main >actual &&
	test_cmp expect actual &&

	but show-ref -d --verify refs/heads/main >actual &&
	test_cmp expect actual &&

	test_must_fail but show-ref -d --verify main >actual &&
	test_must_be_empty actual &&

	test_must_fail but show-ref -d --verify heads/main >actual &&
	test_must_be_empty actual &&

	test_must_fail but show-ref --verify -d A C >actual &&
	test_must_be_empty actual &&

	test_must_fail but show-ref --verify -d tags/A tags/C >actual &&
	test_must_be_empty actual

'

test_expect_success 'show-ref --heads, --tags, --head, pattern' '
	for branch in B main side
	do
		echo $(but rev-parse refs/heads/$branch) refs/heads/$branch || return 1
	done >expect.heads &&
	but show-ref --heads >actual &&
	test_cmp expect.heads actual &&

	for tag in A B C
	do
		echo $(but rev-parse refs/tags/$tag) refs/tags/$tag || return 1
	done >expect.tags &&
	but show-ref --tags >actual &&
	test_cmp expect.tags actual &&

	cat expect.heads expect.tags >expect &&
	but show-ref --heads --tags >actual &&
	test_cmp expect actual &&

	{
		echo $(but rev-parse HEAD) HEAD &&
		cat expect.heads expect.tags
	} >expect &&
	but show-ref --heads --tags --head >actual &&
	test_cmp expect actual &&

	{
		echo $(but rev-parse HEAD) HEAD &&
		echo $(but rev-parse refs/heads/B) refs/heads/B &&
		echo $(but rev-parse refs/tags/B) refs/tags/B
	} >expect &&
	but show-ref --head B >actual &&
	test_cmp expect actual &&

	{
		echo $(but rev-parse HEAD) HEAD &&
		echo $(but rev-parse refs/heads/B) refs/heads/B &&
		echo $(but rev-parse refs/tags/B) refs/tags/B &&
		echo $(but rev-parse refs/tags/B^0) "refs/tags/B^{}"
	} >expect &&
	but show-ref --head -d B >actual &&
	test_cmp expect actual
'

test_expect_success 'show-ref --verify HEAD' '
	echo $(but rev-parse HEAD) HEAD >expect &&
	but show-ref --verify HEAD >actual &&
	test_cmp expect actual &&

	but show-ref --verify -q HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref --verify with dangling ref' '
	sha1_file() {
		echo "$*" | sed "s#..#.but/objects/&/#"
	} &&

	remove_object() {
		file=$(sha1_file "$*") &&
		test -e "$file" &&
		rm -f "$file"
	} &&

	test_when_finished "rm -rf dangling" &&
	(
		but init dangling &&
		cd dangling &&
		test_cummit dangling &&
		sha=$(but rev-parse refs/tags/dangling) &&
		remove_object $sha &&
		test_must_fail but show-ref --verify refs/tags/dangling
	)
'

test_done
