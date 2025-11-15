#!/bin/sh
#

test_description='git rebase --trailer integration tests
We verify that --trailer works with the merge backend,
and that it is rejected early when the apply backend is requested.'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh # test_commit_message, helpers

REVIEWED_BY_TRAILER="Reviewed-by: Dev <dev@example.com>"

expect_trailer_msg() {
	test_commit_message "$1" <<-EOF
	$2

	${3:-$REVIEWED_BY_TRAILER}
	EOF
}

test_expect_success 'setup repo with a small history' '
	git commit --allow-empty -m "Initial empty commit" &&
	test_commit first file a &&
	test_commit second file &&
	git checkout -b conflict-branch first &&
	test_commit file-2 file-2 &&
	test_commit conflict file &&
	test_commit third file
'

test_expect_success 'apply backend is rejected with --trailer' '
	head_before=$(git rev-parse HEAD) &&
	test_expect_code 128 \
	git rebase --apply --trailer "$REVIEWED_BY_TRAILER" \
				HEAD^ 2>err &&
	test_grep "fatal: --trailer requires the merge backend" err &&
	test_cmp_rev HEAD $head_before
'

test_expect_success 'reject empty --trailer argument' '
	test_expect_code 128 git rebase -m --trailer "" HEAD^ 2>err &&
	test_grep "empty --trailer" err
'

test_expect_success 'reject trailer with missing key before separator' '
	test_expect_code 128 git rebase -m --trailer ": no-key" HEAD^ 2>err &&
	test_grep "missing key before separator" err
'

test_expect_success 'allow trailer with missing value after separator' '
	git rebase -m --trailer "Acked-by:" HEAD~1 third &&
	sed -e "s/_/ /g" <<-\EOF >expect &&
	third

	Acked-by:_
	EOF
	test_commit_message HEAD expect
'

test_expect_success 'CLI trailer duplicates allowed; replace policy keeps last' '
	git -c trailer.Bug.ifexists=replace -c trailer.Bug.ifmissing=add \
		rebase -m --trailer "Bug: 123" --trailer "Bug: 456" HEAD~1 third &&
	cat >expect <<-\EOF &&
	third

	Bug: 456
	EOF
	test_commit_message HEAD expect
'

test_expect_success 'multiple Signed-off-by trailers all preserved' '
	git rebase -m \
			--trailer "Signed-off-by: Dev A <a@example.com>" \
			--trailer "Signed-off-by: Dev B <b@example.com>" HEAD~1 third &&
	cat >expect <<-\EOF &&
	third

	Signed-off-by: Dev A <a@example.com>
	Signed-off-by: Dev B <b@example.com>
	EOF
	test_commit_message HEAD expect
'

test_expect_success 'rebase -m --trailer adds trailer after conflicts' '
	git checkout -B conflict-branch third &&
	test_commit fourth file &&
	test_must_fail git rebase -m \
			--trailer "$REVIEWED_BY_TRAILER" \
			second &&
	git checkout --theirs file &&
	git add file &&
	git rebase --continue &&
	expect_trailer_msg HEAD "fourth" &&
	expect_trailer_msg HEAD^ "third"
'

test_expect_success '--trailer handles fixup commands in todo list' '
	git checkout -B fixup-trailer HEAD &&
	test_commit fixup-base base &&
	test_commit fixup-second second &&
	first_short=$(git rev-parse --short fixup-base) &&
	second_short=$(git rev-parse --short fixup-second) &&
	cat >todo <<EOF &&
pick $first_short fixup-base
fixup $second_short fixup-second
EOF
	(
		set_replace_editor todo &&
		git rebase -i --trailer "$REVIEWED_BY_TRAILER" HEAD~2
	) &&
	expect_trailer_msg HEAD "fixup-base" &&
	git reset --hard fixup-second &&
	cat >todo <<EOF &&
pick $first_short fixup-base
fixup -C $second_short fixup-second
EOF
	(
		set_replace_editor todo &&
		git rebase -i --trailer "$REVIEWED_BY_TRAILER" HEAD~2
	) &&
	expect_trailer_msg HEAD "fixup-second"
'

test_expect_success 'rebase --root --trailer updates every commit' '
	git checkout first &&
	git -c trailer.review.key=Reviewed-by rebase --root \
		--trailer=review="Dev <dev@example.com>" &&
	expect_trailer_msg HEAD  "first" &&
	expect_trailer_msg HEAD^ "Initial empty commit"
'
test_done
