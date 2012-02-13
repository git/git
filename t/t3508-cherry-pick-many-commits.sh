#!/bin/sh

test_description='test cherry-picking many commits'

. ./test-lib.sh

check_head_differs_from() {
	head=$(git rev-parse --verify HEAD) &&
	arg=$(git rev-parse --verify "$1") &&
	test "$head" != "$arg"
}

check_head_equals() {
	head=$(git rev-parse --verify HEAD) &&
	arg=$(git rev-parse --verify "$1") &&
	test "$head" = "$arg"
}

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
	cat <<-\EOF >expected &&
	[master OBJID] second
	 Author: A U Thor <author@example.com>
	 1 file changed, 1 insertion(+)
	[master OBJID] third
	 Author: A U Thor <author@example.com>
	 1 file changed, 1 insertion(+)
	[master OBJID] fourth
	 Author: A U Thor <author@example.com>
	 1 file changed, 1 insertion(+)
	EOF

	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick first..fourth >actual &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&

	sed -e "s/$_x05[0-9a-f][0-9a-f]/OBJID/" <actual >actual.fuzzy &&
	test_cmp expected actual.fuzzy &&
	check_head_differs_from fourth
'

test_expect_success 'cherry-pick --strategy resolve first..fourth works' '
	cat <<-\EOF >expected &&
	Trying simple merge.
	[master OBJID] second
	 Author: A U Thor <author@example.com>
	 1 file changed, 1 insertion(+)
	Trying simple merge.
	[master OBJID] third
	 Author: A U Thor <author@example.com>
	 1 file changed, 1 insertion(+)
	Trying simple merge.
	[master OBJID] fourth
	 Author: A U Thor <author@example.com>
	 1 file changed, 1 insertion(+)
	EOF

	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick --strategy resolve first..fourth >actual &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	sed -e "s/$_x05[0-9a-f][0-9a-f]/OBJID/" <actual >actual.fuzzy &&
	test_cmp expected actual.fuzzy &&
	check_head_differs_from fourth
'

test_expect_success 'cherry-pick --ff first..fourth works' '
	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick --ff first..fourth &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	check_head_equals fourth
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
	check_head_differs_from fourth
'

test_expect_success 'cherry-pick --stdin works' '
	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git rev-list --reverse first..fourth | git cherry-pick --stdin &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	check_head_differs_from fourth
'

test_done
