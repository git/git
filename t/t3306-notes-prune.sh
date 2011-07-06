#!/bin/sh

test_description='Test git notes prune'

. ./test-lib.sh

test_expect_success 'setup: create a few commits with notes' '

	: > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m 1st &&
	git notes add -m "Note #1" &&
	: > file2 &&
	git add file2 &&
	test_tick &&
	git commit -m 2nd &&
	git notes add -m "Note #2" &&
	: > file3 &&
	git add file3 &&
	test_tick &&
	git commit -m 3rd &&
	COMMIT_FILE=.git/objects/5e/e1c35e83ea47cd3cc4f8cbee0568915fbbbd29 &&
	test -f $COMMIT_FILE &&
	test-chmtime =+0 $COMMIT_FILE &&
	git notes add -m "Note #3"
'

cat > expect <<END_OF_LOG
commit 5ee1c35e83ea47cd3cc4f8cbee0568915fbbbd29
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:15:13 2005 -0700

    3rd

Notes:
    Note #3

commit 08341ad9e94faa089d60fd3f523affb25c6da189
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:14:13 2005 -0700

    2nd

Notes:
    Note #2

commit ab5f302035f2e7aaf04265f08b42034c23256e1f
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

	test_must_fail git cat-file -p 5ee1c35e83ea47cd3cc4f8cbee0568915fbbbd29 &&
	git cat-file -p 08341ad9e94faa089d60fd3f523affb25c6da189 &&
	git cat-file -p ab5f302035f2e7aaf04265f08b42034c23256e1f
'

test_expect_success 'verify that notes are still present' '

	git notes show 5ee1c35e83ea47cd3cc4f8cbee0568915fbbbd29 &&
	git notes show 08341ad9e94faa089d60fd3f523affb25c6da189 &&
	git notes show ab5f302035f2e7aaf04265f08b42034c23256e1f
'

test_expect_success 'prune -n does not remove notes' '

	git notes list > expect &&
	git notes prune -n &&
	git notes list > actual &&
	test_cmp expect actual
'

cat > expect <<EOF
5ee1c35e83ea47cd3cc4f8cbee0568915fbbbd29
EOF

test_expect_success 'prune -n lists prunable notes' '


	git notes prune -n > actual &&
	test_cmp expect actual
'


test_expect_success 'prune notes' '

	git notes prune
'

test_expect_success 'verify that notes are gone' '

	test_must_fail git notes show 5ee1c35e83ea47cd3cc4f8cbee0568915fbbbd29 &&
	git notes show 08341ad9e94faa089d60fd3f523affb25c6da189 &&
	git notes show ab5f302035f2e7aaf04265f08b42034c23256e1f
'

test_expect_success 'remove some commits' '

	git reset --hard HEAD~1 &&
	git reflog expire --expire=now HEAD &&
	git gc --prune=now
'

cat > expect <<EOF
08341ad9e94faa089d60fd3f523affb25c6da189
EOF

test_expect_success 'prune -v notes' '

	git notes prune -v > actual &&
	test_cmp expect actual
'

test_expect_success 'verify that notes are gone' '

	test_must_fail git notes show 5ee1c35e83ea47cd3cc4f8cbee0568915fbbbd29 &&
	test_must_fail git notes show 08341ad9e94faa089d60fd3f523affb25c6da189 &&
	git notes show ab5f302035f2e7aaf04265f08b42034c23256e1f
'

test_done
