#!/bin/sh

test_description='test cherry-picking many commits'

. ./test-lib.sh

test_expect_success setup '
	echo first > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m "first" &&
	git tag first &&

	git checkout -b other &&
	for val in second third fourth
	do
		echo $val >> file1 &&
		git add file1 &&
		test_tick &&
		git commit -m "$val" &&
		git tag $val
	done
'

test_expect_success 'cherry-pick first..fourth works' '
	cat <<-EOF >expected &&
	Finished cherry-pick of commit $(git rev-parse --short second).
	Finished cherry-pick of commit $(git rev-parse --short third).
	Finished cherry-pick of commit $(git rev-parse --short fourth).
	EOF

	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick first..fourth 2>actual &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	test_cmp expected actual &&
	test "$(git rev-parse --verify HEAD)" != "$(git rev-parse --verify fourth)"
'

test_expect_success 'cherry-pick --strategy resolve first..fourth works' '
	cat <<-EOF >expected &&
	Finished cherry-pick of commit $(git rev-parse --short second) with strategy resolve.
	Finished cherry-pick of commit $(git rev-parse --short third) with strategy resolve.
	Finished cherry-pick of commit $(git rev-parse --short fourth) with strategy resolve.
	EOF

	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick --strategy resolve first..fourth 2>actual &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	test_cmp expected actual &&
	test "$(git rev-parse --verify HEAD)" != "$(git rev-parse --verify fourth)"
'

test_expect_success 'cherry-pick --ff first..fourth works' '
	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick --ff first..fourth &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	test "$(git rev-parse --verify HEAD)" = "$(git rev-parse --verify fourth)"
'

test_expect_success 'cherry-pick -n first..fourth works' '
	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick -n first..fourth &&
	git diff --quiet other &&
	git diff --cached --quiet other &&
	git diff --quiet HEAD first
'

test_expect_success 'revert first..fourth works' '
	git checkout -f master &&
	git reset --hard fourth &&
	test_tick &&
	git revert first..fourth &&
	git diff --quiet first &&
	git diff --cached --quiet first &&
	git diff --quiet HEAD first
'

test_expect_success 'revert ^first fourth works' '
	git checkout -f master &&
	git reset --hard fourth &&
	test_tick &&
	git revert ^first fourth &&
	git diff --quiet first &&
	git diff --cached --quiet first &&
	git diff --quiet HEAD first
'

test_expect_success 'revert fourth fourth~1 fourth~2 works' '
	git checkout -f master &&
	git reset --hard fourth &&
	test_tick &&
	git revert fourth fourth~1 fourth~2 &&
	git diff --quiet first &&
	git diff --cached --quiet first &&
	git diff --quiet HEAD first
'

test_expect_success 'cherry-pick -3 fourth works' '
	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick -3 fourth &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	test "$(git rev-parse --verify HEAD)" != "$(git rev-parse --verify fourth)"
'

test_expect_success 'cherry-pick --stdin works' '
	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git rev-list --reverse first..fourth | git cherry-pick --stdin &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	test "$(git rev-parse --verify HEAD)" != "$(git rev-parse --verify fourth)"
'

test_done
