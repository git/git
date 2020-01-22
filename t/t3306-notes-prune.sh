#!/bin/sh

test_description='Test git notes prune'

. ./test-lib.sh

test_expect_success 'setup: create a few commits with notes' '

	: > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m 1st &&
	git notes add -m "Note #1" &&
	first=$(git rev-parse HEAD) &&
	: > file2 &&
	git add file2 &&
	test_tick &&
	git commit -m 2nd &&
	git notes add -m "Note #2" &&
	second=$(git rev-parse HEAD) &&
	: > file3 &&
	git add file3 &&
	test_tick &&
	git commit -m 3rd &&
	third=$(git rev-parse HEAD) &&
	COMMIT_FILE=$(echo $third | sed "s!^..!.git/objects/&/!") &&
	test -f $COMMIT_FILE &&
	test-tool chmtime =+0 $COMMIT_FILE &&
	git notes add -m "Note #3"
'

cat > expect <<END_OF_LOG
commit $third
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:15:13 2005 -0700

    3rd

Notes:
    Note #3

commit $second
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:14:13 2005 -0700

    2nd

Notes:
    Note #2

commit $first
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:13:13 2005 -0700

    1st

Notes:
    Note #1
END_OF_LOG

test_expect_success 'verify commits and notes' '

	git log > actual &&
	test_cmp expect actual
'

test_expect_success 'remove some commits' '

	git reset --hard HEAD~1 &&
	git reflog expire --expire=now HEAD &&
	git gc --prune=now
'

test_expect_success 'verify that commits are gone' '

	test_must_fail git cat-file -p $third &&
	git cat-file -p $second &&
	git cat-file -p $first
'

test_expect_success 'verify that notes are still present' '

	git notes show $third &&
	git notes show $second &&
	git notes show $first
'

test_expect_success 'prune -n does not remove notes' '

	git notes list > expect &&
	git notes prune -n &&
	git notes list > actual &&
	test_cmp expect actual
'


test_expect_success 'prune -n lists prunable notes' '

	echo $third >expect &&
	git notes prune -n > actual &&
	test_cmp expect actual
'


test_expect_success 'prune notes' '

	git notes prune
'

test_expect_success 'verify that notes are gone' '

	test_must_fail git notes show $third &&
	git notes show $second &&
	git notes show $first
'

test_expect_success 'remove some commits' '

	git reset --hard HEAD~1 &&
	git reflog expire --expire=now HEAD &&
	git gc --prune=now
'

test_expect_success 'prune -v notes' '

	echo $second >expect &&
	git notes prune -v > actual &&
	test_cmp expect actual
'

test_expect_success 'verify that notes are gone' '

	test_must_fail git notes show $third &&
	test_must_fail git notes show $second &&
	git notes show $first
'

test_done
