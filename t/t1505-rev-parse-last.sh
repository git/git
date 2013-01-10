#!/bin/sh

test_description='test @{-N} syntax'

. ./test-lib.sh


make_commit () {
	echo "$1" > "$1" &&
	git add "$1" &&
	git commit -m "$1"
}


test_expect_success 'setup' '

	make_commit 1 &&
	git branch side &&
	make_commit 2 &&
	make_commit 3 &&
	git checkout side &&
	make_commit 4 &&
	git merge master &&
	git checkout master

'

# 1 -- 2 -- 3 master
#  \         \
#   \         \
#    --- 4 --- 5 side
#
# and 'side' should be the last branch

test_expect_success '@{-1} works' '
	test_cmp_rev side @{-1}
'

test_expect_success '@{-1}~2 works' '
	test_cmp_rev side~2 @{-1}~2
'

test_expect_success '@{-1}^2 works' '
	test_cmp_rev side^2 @{-1}^2
'

test_expect_success '@{-1}@{1} works' '
	test_cmp_rev side@{1} @{-1}@{1}
'

test_expect_success '@{-2} works' '
	test_cmp_rev master @{-2}
'

test_expect_success '@{-3} fails' '
	test_must_fail git rev-parse @{-3}
'

test_done


