#!/bin/sh

test_description='Test but notes prune'

. ./test-lib.sh

test_expect_success 'setup: create a few cummits with notes' '

	: > file1 &&
	but add file1 &&
	test_tick &&
	but cummit -m 1st &&
	but notes add -m "Note #1" &&
	first=$(but rev-parse HEAD) &&
	: > file2 &&
	but add file2 &&
	test_tick &&
	but cummit -m 2nd &&
	but notes add -m "Note #2" &&
	second=$(but rev-parse HEAD) &&
	: > file3 &&
	but add file3 &&
	test_tick &&
	but cummit -m 3rd &&
	third=$(but rev-parse HEAD) &&
	CUMMIT_FILE=$(echo $third | sed "s!^..!.but/objects/&/!") &&
	test -f $CUMMIT_FILE &&
	test-tool chmtime =+0 $CUMMIT_FILE &&
	but notes add -m "Note #3"
'

cat > expect <<END_OF_LOG
cummit $third
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:15:13 2005 -0700

    3rd

Notes:
    Note #3

cummit $second
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:14:13 2005 -0700

    2nd

Notes:
    Note #2

cummit $first
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:13:13 2005 -0700

    1st

Notes:
    Note #1
END_OF_LOG

test_expect_success 'verify cummits and notes' '

	but log > actual &&
	test_cmp expect actual
'

test_expect_success 'remove some cummits' '

	but reset --hard HEAD~1 &&
	but reflog expire --expire=now HEAD &&
	but gc --prune=now
'

test_expect_success 'verify that cummits are gone' '

	test_must_fail but cat-file -p $third &&
	but cat-file -p $second &&
	but cat-file -p $first
'

test_expect_success 'verify that notes are still present' '

	but notes show $third &&
	but notes show $second &&
	but notes show $first
'

test_expect_success 'prune -n does not remove notes' '

	but notes list > expect &&
	but notes prune -n &&
	but notes list > actual &&
	test_cmp expect actual
'


test_expect_success 'prune -n lists prunable notes' '

	echo $third >expect &&
	but notes prune -n > actual &&
	test_cmp expect actual
'


test_expect_success 'prune notes' '

	but notes prune
'

test_expect_success 'verify that notes are gone' '

	test_must_fail but notes show $third &&
	but notes show $second &&
	but notes show $first
'

test_expect_success 'remove some cummits' '

	but reset --hard HEAD~1 &&
	but reflog expire --expire=now HEAD &&
	but gc --prune=now
'

test_expect_success 'prune -v notes' '

	echo $second >expect &&
	but notes prune -v > actual &&
	test_cmp expect actual
'

test_expect_success 'verify that notes are gone' '

	test_must_fail but notes show $third &&
	test_must_fail but notes show $second &&
	but notes show $first
'

test_done
