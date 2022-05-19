#!/bin/sh

test_description='test cherry-picking many cummits'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check_head_differs_from() {
	test_cmp_rev ! HEAD "$1"
}

check_head_equals() {
	test_cmp_rev HEAD "$1"
}

test_expect_success setup '
	echo first > file1 &&
	but add file1 &&
	test_tick &&
	but cummit -m "first" &&
	but tag first &&

	but checkout -b other &&
	for val in second third fourth
	do
		echo $val >> file1 &&
		but add file1 &&
		test_tick &&
		but cummit -m "$val" &&
		but tag $val || return 1
	done
'

test_expect_success 'cherry-pick first..fourth works' '
	but checkout -f main &&
	but reset --hard first &&
	test_tick &&
	but cherry-pick first..fourth &&
	but diff --quiet other &&
	but diff --quiet HEAD other &&
	check_head_differs_from fourth
'

test_expect_success 'cherry-pick three one two works' '
	but checkout -f first &&
	test_cummit one &&
	test_cummit two &&
	test_cummit three &&
	but checkout -f main &&
	but reset --hard first &&
	but cherry-pick three one two &&
	but diff --quiet three &&
	but diff --quiet HEAD three &&
	test "$(but log --reverse --format=%s first..)" = "three
one
two"
'

test_expect_success 'cherry-pick three one two: fails' '
	but checkout -f main &&
	but reset --hard first &&
	test_must_fail but cherry-pick three one two:
'

test_expect_success 'output to keep user entertained during multi-pick' '
	cat <<-\EOF >expected &&
	[main OBJID] second
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:14:13 2005 -0700
	 1 file changed, 1 insertion(+)
	[main OBJID] third
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:15:13 2005 -0700
	 1 file changed, 1 insertion(+)
	[main OBJID] fourth
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:16:13 2005 -0700
	 1 file changed, 1 insertion(+)
	EOF

	but checkout -f main &&
	but reset --hard first &&
	test_tick &&
	but cherry-pick first..fourth >actual &&
	sed -e "s/$_x05[0-9a-f][0-9a-f]/OBJID/" <actual >actual.fuzzy &&
	test_line_count -ge 3 actual.fuzzy &&
	test_cmp expected actual.fuzzy
'

test_expect_success 'cherry-pick --strategy resolve first..fourth works' '
	but checkout -f main &&
	but reset --hard first &&
	test_tick &&
	but cherry-pick --strategy resolve first..fourth &&
	but diff --quiet other &&
	but diff --quiet HEAD other &&
	check_head_differs_from fourth
'

test_expect_success 'output during multi-pick indicates merge strategy' '
	cat <<-\EOF >expected &&
	Trying simple merge.
	[main OBJID] second
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:14:13 2005 -0700
	 1 file changed, 1 insertion(+)
	Trying simple merge.
	[main OBJID] third
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:15:13 2005 -0700
	 1 file changed, 1 insertion(+)
	Trying simple merge.
	[main OBJID] fourth
	 Author: A U Thor <author@example.com>
	 Date: Thu Apr 7 15:16:13 2005 -0700
	 1 file changed, 1 insertion(+)
	EOF

	but checkout -f main &&
	but reset --hard first &&
	test_tick &&
	but cherry-pick --strategy resolve first..fourth >actual &&
	sed -e "s/$_x05[0-9a-f][0-9a-f]/OBJID/" <actual >actual.fuzzy &&
	test_cmp expected actual.fuzzy
'

test_expect_success 'cherry-pick --ff first..fourth works' '
	but checkout -f main &&
	but reset --hard first &&
	test_tick &&
	but cherry-pick --ff first..fourth &&
	but diff --quiet other &&
	but diff --quiet HEAD other &&
	check_head_equals fourth
'

test_expect_success 'cherry-pick -n first..fourth works' '
	but checkout -f main &&
	but reset --hard first &&
	test_tick &&
	but cherry-pick -n first..fourth &&
	but diff --quiet other &&
	but diff --cached --quiet other &&
	but diff --quiet HEAD first
'

test_expect_success 'revert first..fourth works' '
	but checkout -f main &&
	but reset --hard fourth &&
	test_tick &&
	but revert first..fourth &&
	but diff --quiet first &&
	but diff --cached --quiet first &&
	but diff --quiet HEAD first
'

test_expect_success 'revert ^first fourth works' '
	but checkout -f main &&
	but reset --hard fourth &&
	test_tick &&
	but revert ^first fourth &&
	but diff --quiet first &&
	but diff --cached --quiet first &&
	but diff --quiet HEAD first
'

test_expect_success 'revert fourth fourth~1 fourth~2 works' '
	but checkout -f main &&
	but reset --hard fourth &&
	test_tick &&
	but revert fourth fourth~1 fourth~2 &&
	but diff --quiet first &&
	but diff --cached --quiet first &&
	but diff --quiet HEAD first
'

test_expect_success 'cherry-pick -3 fourth works' '
	but checkout -f main &&
	but reset --hard first &&
	test_tick &&
	but cherry-pick -3 fourth &&
	but diff --quiet other &&
	but diff --quiet HEAD other &&
	check_head_differs_from fourth
'

test_expect_success 'cherry-pick --stdin works' '
	but checkout -f main &&
	but reset --hard first &&
	test_tick &&
	but rev-list --reverse first..fourth | but cherry-pick --stdin &&
	but diff --quiet other &&
	but diff --quiet HEAD other &&
	check_head_differs_from fourth
'

test_done
