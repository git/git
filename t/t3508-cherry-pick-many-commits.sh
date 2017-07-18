#!/bin/sh

test_description='test cherry-picking many commits'

. ./test-lib.sh

check_head_differs_from() {
	! test_cmp_rev HEAD "$1"
}

check_head_equals() {
	test_cmp_rev HEAD "$1"
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
	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick first..fourth &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	check_head_differs_from fourth
'

test_expect_success 'cherry-pick three one two works' '
	git checkout -f first &&
	test_commit one &&
	test_commit two &&
	test_commit three &&
	git checkout -f master &&
	git reset --hard first &&
	git cherry-pick three one two &&
	git diff --quiet three &&
	git diff --quiet HEAD three &&
	test "$(git log --reverse --format=%s first..)" = "three
one
two"
'

test_expect_success 'cherry-pick three one two: fails' '
	git checkout -f master &&
	git reset --hard first &&
	test_must_fail git cherry-pick three one two:
'

test_expect_success 'output to keep user entertained during multi-pick' '
	cat <<-\EOF >expected &&
	[master OBJID] second
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:14:13 2005 -0700
	 1 file changed, 1 insertion(+)
	[master OBJID] third
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:15:13 2005 -0700
	 1 file changed, 1 insertion(+)
	[master OBJID] fourth
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:16:13 2005 -0700
	 1 file changed, 1 insertion(+)
	EOF

	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick first..fourth >actual &&
	sed -e "s/$_x05[0-9a-f][0-9a-f]/OBJID/" <actual >actual.fuzzy &&
	test_line_count -ge 3 actual.fuzzy &&
	test_i18ncmp expected actual.fuzzy
'

test_expect_success 'cherry-pick --strategy resolve first..fourth works' '
	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick --strategy resolve first..fourth &&
	git diff --quiet other &&
	git diff --quiet HEAD other &&
	check_head_differs_from fourth
'

test_expect_success 'output during multi-pick indicates merge strategy' '
	cat <<-\EOF >expected &&
	Trying simple merge.
	[master OBJID] second
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:14:13 2005 -0700
	 1 file changed, 1 insertion(+)
	Trying simple merge.
	[master OBJID] third
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:15:13 2005 -0700
	 1 file changed, 1 insertion(+)
	Trying simple merge.
	[master OBJID] fourth
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:16:13 2005 -0700
	 1 file changed, 1 insertion(+)
	EOF

	git checkout -f master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick --strategy resolve first..fourth >actual &&
	sed -e "s/$_x05[0-9a-f][0-9a-f]/OBJID/" <actual >actual.fuzzy &&
	test_i18ncmp expected actual.fuzzy
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
