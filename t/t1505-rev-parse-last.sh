#!/bin/sh

test_description='test @{-N} syntax'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh


make_cummit () {
	echo "$1" > "$1" &&
	git add "$1" &&
	git cummit -m "$1"
}


test_expect_success 'setup' '

	make_cummit 1 &&
	git branch side &&
	make_cummit 2 &&
	make_cummit 3 &&
	git checkout side &&
	make_cummit 4 &&
	git merge main &&
	git checkout main

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
	test_must_fail git rev-parse @{-3}
'

test_done


