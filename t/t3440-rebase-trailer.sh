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
SP=" "

test_expect_success 'setup repo with a small history' '
	git commit --allow-empty -m "Initial empty commit" &&
	test_commit first file a &&
	test_commit second file &&
	git checkout -b conflict-branch first &&
	test_commit file-2 file-2 &&
	test_commit conflict file &&
	test_commit third file &&
	git checkout main
'

test_expect_success 'apply backend is rejected with --trailer' '
	git checkout -B apply-backend third &&
	test_expect_code 128 \
		git rebase --apply --trailer "$REVIEWED_BY_TRAILER" HEAD^ 2>err &&
	test_grep "fatal: --trailer requires the merge backend" err
'

test_expect_success 'reject empty --trailer argument' '
	git checkout -B empty-trailer third &&
	test_expect_code 128 git rebase --trailer "" HEAD^ 2>err &&
	test_grep "empty --trailer" err
'

test_expect_success 'reject trailer with missing key before separator' '
	git checkout -B missing-key third &&
	test_expect_code 128 git rebase --trailer ": no-key" HEAD^ 2>err &&
	test_grep "missing key before separator" err
'

test_expect_success 'allow trailer with missing value after separator' '
	git checkout -B missing-value third &&
	git rebase --trailer "Acked-by:" HEAD^ &&
	test_commit_message HEAD <<-EOF
	third

	Acked-by:${SP}
	EOF
'

test_expect_success 'CLI trailer duplicates allowed; replace policy keeps last' '
	git checkout -B replace-policy third &&
	git -c trailer.Bug.ifexists=replace -c trailer.Bug.ifmissing=add \
		rebase --trailer "Bug: 123" --trailer "Bug: 456" HEAD^ &&
	test_commit_message HEAD <<-EOF
	third

	Bug: 456
	EOF
'

test_expect_success 'multiple Signed-off-by trailers all preserved' '
	git checkout -B multiple-signoff third &&
	git rebase --trailer "Signed-off-by: Dev A <a@example.com>" \
		--trailer "Signed-off-by: Dev B <b@example.com>" HEAD^ &&
	test_commit_message HEAD <<-EOF
	third

	Signed-off-by: Dev A <a@example.com>
	Signed-off-by: Dev B <b@example.com>
	EOF
'

test_expect_success 'rebase --trailer adds trailer after conflicts' '
	git checkout -B trailer-conflict third &&
	test_commit fourth file &&
	test_must_fail git rebase --trailer "$REVIEWED_BY_TRAILER" second &&
	git checkout --theirs file &&
	git add file &&
	git rebase --continue &&
	test_commit_message HEAD <<-EOF &&
	fourth

	$REVIEWED_BY_TRAILER
	EOF
	test_commit_message HEAD^ <<-EOF
	third

	$REVIEWED_BY_TRAILER
	EOF
'

test_expect_success '--trailer handles fixup commands in todo list' '
	git checkout -B fixup-trailer third &&
	test_commit fixup-base base &&
	test_commit fixup-second second &&
	cat >todo <<-\EOF &&
	pick fixup-base fixup-base
	fixup fixup-second fixup-second
	EOF
	(
		set_replace_editor todo &&
		git rebase -i --trailer "$REVIEWED_BY_TRAILER" HEAD~2
	) &&
	test_commit_message HEAD <<-EOF &&
	fixup-base

	$REVIEWED_BY_TRAILER
	EOF
	git reset --hard fixup-second &&
	cat >todo <<-\EOF &&
	pick fixup-base fixup-base
	fixup -C fixup-second fixup-second
	EOF
	(
		set_replace_editor todo &&
		git rebase -i --trailer "$REVIEWED_BY_TRAILER" HEAD~2
	) &&
	test_commit_message HEAD <<-EOF
	fixup-second

	$REVIEWED_BY_TRAILER
	EOF
'

test_expect_success 'rebase --root honors trailer.<name>.key' '
	git checkout -B root-trailer first &&
	git -c trailer.review.key=Reviewed-by rebase --root \
		--trailer=review="Dev <dev@example.com>" &&
	test_commit_message HEAD <<-EOF &&
	first

	Reviewed-by: Dev <dev@example.com>
	EOF
	test_commit_message HEAD^ <<-EOF
	Initial empty commit

	Reviewed-by: Dev <dev@example.com>
	EOF
'
test_done
