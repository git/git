#!/bin/sh

test_description='show-ref'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	test_commit --annotate A &&
	git checkout -b side &&
	test_commit --annotate B &&
	git checkout main &&
	test_commit C &&
	git branch B A^0
'

test_expect_success 'show-ref' '
	echo $(git rev-parse refs/tags/A) refs/tags/A >expect &&

	git show-ref A >actual &&
	test_cmp expect actual &&

	git show-ref tags/A >actual &&
	test_cmp expect actual &&

	git show-ref refs/tags/A >actual &&
	test_cmp expect actual &&

	test_must_fail git show-ref D >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref -q' '
	git show-ref -q A >actual &&
	test_must_be_empty actual &&

	git show-ref -q tags/A >actual &&
	test_must_be_empty actual &&

	git show-ref -q refs/tags/A >actual &&
	test_must_be_empty actual &&

	test_must_fail git show-ref -q D >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref --verify' '
	echo $(git rev-parse refs/tags/A) refs/tags/A >expect &&

	git show-ref --verify refs/tags/A >actual &&
	test_cmp expect actual &&

	test_must_fail git show-ref --verify A >actual &&
	test_must_be_empty actual &&

	test_must_fail git show-ref --verify tags/A >actual &&
	test_must_be_empty actual &&

	test_must_fail git show-ref --verify D >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref --verify -q' '
	git show-ref --verify -q refs/tags/A >actual &&
	test_must_be_empty actual &&

	test_must_fail git show-ref --verify -q A >actual &&
	test_must_be_empty actual &&

	test_must_fail git show-ref --verify -q tags/A >actual &&
	test_must_be_empty actual &&

	test_must_fail git show-ref --verify -q D >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref -d' '
	{
		echo $(git rev-parse refs/tags/A) refs/tags/A &&
		echo $(git rev-parse refs/tags/A^0) "refs/tags/A^{}" &&
		echo $(git rev-parse refs/tags/C) refs/tags/C
	} >expect &&
	git show-ref -d A C >actual &&
	test_cmp expect actual &&

	git show-ref -d tags/A tags/C >actual &&
	test_cmp expect actual &&

	git show-ref -d refs/tags/A refs/tags/C >actual &&
	test_cmp expect actual &&

	git show-ref --verify -d refs/tags/A refs/tags/C >actual &&
	test_cmp expect actual &&

	echo $(git rev-parse refs/heads/main) refs/heads/main >expect &&
	git show-ref -d main >actual &&
	test_cmp expect actual &&

	git show-ref -d heads/main >actual &&
	test_cmp expect actual &&

	git show-ref -d refs/heads/main >actual &&
	test_cmp expect actual &&

	git show-ref -d --verify refs/heads/main >actual &&
	test_cmp expect actual &&

	test_must_fail git show-ref -d --verify main >actual &&
	test_must_be_empty actual &&

	test_must_fail git show-ref -d --verify heads/main >actual &&
	test_must_be_empty actual &&

	test_must_fail git show-ref --verify -d A C >actual &&
	test_must_be_empty actual &&

	test_must_fail git show-ref --verify -d tags/A tags/C >actual &&
	test_must_be_empty actual

'

test_expect_success 'show-ref --heads, --tags, --head, pattern' '
	for branch in B main side
	do
		echo $(git rev-parse refs/heads/$branch) refs/heads/$branch || return 1
	done >expect.heads &&
	git show-ref --heads >actual &&
	test_cmp expect.heads actual &&

	for tag in A B C
	do
		echo $(git rev-parse refs/tags/$tag) refs/tags/$tag || return 1
	done >expect.tags &&
	git show-ref --tags >actual &&
	test_cmp expect.tags actual &&

	cat expect.heads expect.tags >expect &&
	git show-ref --heads --tags >actual &&
	test_cmp expect actual &&

	{
		echo $(git rev-parse HEAD) HEAD &&
		cat expect.heads expect.tags
	} >expect &&
	git show-ref --heads --tags --head >actual &&
	test_cmp expect actual &&

	{
		echo $(git rev-parse HEAD) HEAD &&
		echo $(git rev-parse refs/heads/B) refs/heads/B &&
		echo $(git rev-parse refs/tags/B) refs/tags/B
	} >expect &&
	git show-ref --head B >actual &&
	test_cmp expect actual &&

	{
		echo $(git rev-parse HEAD) HEAD &&
		echo $(git rev-parse refs/heads/B) refs/heads/B &&
		echo $(git rev-parse refs/tags/B) refs/tags/B &&
		echo $(git rev-parse refs/tags/B^0) "refs/tags/B^{}"
	} >expect &&
	git show-ref --head -d B >actual &&
	test_cmp expect actual
'

test_expect_success 'show-ref --verify HEAD' '
	echo $(git rev-parse HEAD) HEAD >expect &&
	git show-ref --verify HEAD >actual &&
	test_cmp expect actual &&

	git show-ref --verify -q HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref --verify with dangling ref' '
	sha1_file() {
		echo "$*" | sed "s#..#.git/objects/&/#"
	} &&

	remove_object() {
		file=$(sha1_file "$*") &&
		test -e "$file" &&
		rm -f "$file"
	} &&

	test_when_finished "rm -rf dangling" &&
	(
		git init dangling &&
		cd dangling &&
		test_commit dangling &&
		sha=$(git rev-parse refs/tags/dangling) &&
		remove_object $sha &&
		test_must_fail git show-ref --verify refs/tags/dangling
	)
'

test_done
