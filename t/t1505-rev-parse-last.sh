#!/bin/sh

test_description='test @{-N} syntax'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh


make_cummit () {
	echo "$1" > "$1" &&
	but add "$1" &&
	but cummit -m "$1"
}


test_expect_success 'setup' '

	make_cummit 1 &&
	but branch side &&
	make_cummit 2 &&
	make_cummit 3 &&
	but checkout side &&
	make_cummit 4 &&
	but merge main &&
	but checkout main

'

# 1 -- 2 -- 3 main
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
	test_cmp_rev main @{-2}
'

test_expect_success '@{-3} fails' '
	test_must_fail but rev-parse @{-3}
'

test_done


