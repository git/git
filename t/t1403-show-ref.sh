#!/bin/sh

test_description='show-ref'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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

test_expect_success 'show-ref --branches, --tags, --head, pattern' '
	for branch in B main side
	do
		echo $(git rev-parse refs/heads/$branch) refs/heads/$branch || return 1
	done >expect.branches &&
	git show-ref --branches >actual &&
	test_cmp expect.branches actual &&

	for tag in A B C
	do
		echo $(git rev-parse refs/tags/$tag) refs/tags/$tag || return 1
	done >expect.tags &&
	git show-ref --tags >actual &&
	test_cmp expect.tags actual &&

	cat expect.branches expect.tags >expect &&
	git show-ref --branches --tags >actual &&
	test_cmp expect actual &&

	{
		echo $(git rev-parse HEAD) HEAD &&
		cat expect.branches expect.tags
	} >expect &&
	git show-ref --branches --tags --head >actual &&
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

test_expect_success 'show-ref --heads is deprecated and hidden' '
	test_expect_code 129 git show-ref -h >short-help &&
	test_grep ! -e --heads short-help &&
	git show-ref --heads >actual 2>warning &&
	test_grep ! deprecated warning &&
	test_cmp expect.branches actual
'

test_expect_success 'show-ref --verify HEAD' '
	echo $(git rev-parse HEAD) HEAD >expect &&
	git show-ref --verify HEAD >actual &&
	test_cmp expect actual &&

	git show-ref --verify -q HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'show-ref --verify pseudorefs' '
	git update-ref CHERRY_PICK_HEAD HEAD $ZERO_OID &&
	test_when_finished "git update-ref -d CHERRY_PICK_HEAD" &&
	git show-ref -s --verify HEAD >actual &&
	git show-ref -s --verify CHERRY_PICK_HEAD >expect &&
	test_cmp actual expect
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

test_expect_success 'show-ref sub-modes are mutually exclusive' '
	test_must_fail git show-ref --verify --exclude-existing 2>err &&
	grep "verify" err &&
	grep "exclude-existing" err &&
	grep "cannot be used together" err &&

	test_must_fail git show-ref --verify --exists 2>err &&
	grep "verify" err &&
	grep "exists" err &&
	grep "cannot be used together" err &&

	test_must_fail git show-ref --exclude-existing --exists 2>err &&
	grep "exclude-existing" err &&
	grep "exists" err &&
	grep "cannot be used together" err
'

test_expect_success '--exists with existing reference' '
	git show-ref --exists refs/heads/$GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
'

test_expect_success '--exists with missing reference' '
	test_expect_code 2 git show-ref --exists refs/heads/does-not-exist
'

test_expect_success '--exists does not use DWIM' '
	test_expect_code 2 git show-ref --exists $GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME 2>err &&
	grep "reference does not exist" err
'

test_expect_success '--exists with HEAD' '
	git show-ref --exists HEAD
'

test_expect_success '--exists with bad reference name' '
	test_when_finished "git update-ref -d refs/heads/bad...name" &&
	new_oid=$(git rev-parse HEAD) &&
	test-tool ref-store main update-ref msg refs/heads/bad...name $new_oid $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&
	git show-ref --exists refs/heads/bad...name
'

test_expect_success '--exists with arbitrary symref' '
	test_when_finished "git symbolic-ref -d refs/symref" &&
	git symbolic-ref refs/symref refs/heads/$GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME &&
	git show-ref --exists refs/symref
'

test_expect_success '--exists with dangling symref' '
	test_when_finished "git symbolic-ref -d refs/heads/dangling" &&
	git symbolic-ref refs/heads/dangling refs/heads/does-not-exist &&
	git show-ref --exists refs/heads/dangling
'

test_expect_success '--exists with nonexistent object ID' '
	test-tool ref-store main update-ref msg refs/heads/missing-oid $(test_oid 001) $ZERO_OID REF_SKIP_OID_VERIFICATION &&
	git show-ref --exists refs/heads/missing-oid
'

test_expect_success '--exists with non-commit object' '
	tree_oid=$(git rev-parse HEAD^{tree}) &&
	test-tool ref-store main update-ref msg refs/heads/tree ${tree_oid} $ZERO_OID REF_SKIP_OID_VERIFICATION &&
	git show-ref --exists refs/heads/tree
'

test_expect_success '--exists with directory fails with generic error' '
	cat >expect <<-EOF &&
	error: reference does not exist
	EOF
	test_expect_code 2 git show-ref --exists refs/heads 2>err &&
	test_cmp expect err
'

test_expect_success '--exists with non-existent special ref' '
	test_expect_code 2 git show-ref --exists FETCH_HEAD
'

test_expect_success '--exists with existing special ref' '
	test_when_finished "rm .git/FETCH_HEAD" &&
	git rev-parse HEAD >.git/FETCH_HEAD &&
	git show-ref --exists FETCH_HEAD
'

test_done
