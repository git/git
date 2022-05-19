#!/bin/sh

test_description='previous branch syntax @{-n}'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'branch -d @{-1}' '
	test_cummit A &&
	but checkout -b junk &&
	but checkout - &&
	test "$(but symbolic-ref HEAD)" = refs/heads/main &&
	but branch -d @{-1} &&
	test_must_fail but rev-parse --verify refs/heads/junk
'

test_expect_success 'branch -d @{-12} when there is not enough switches yet' '
	but reflog expire --expire=now &&
	but checkout -b junk2 &&
	but checkout - &&
	test "$(but symbolic-ref HEAD)" = refs/heads/main &&
	test_must_fail but branch -d @{-12} &&
	but rev-parse --verify refs/heads/main
'

test_expect_success 'merge @{-1}' '
	but checkout A &&
	test_cummit B &&
	but checkout A &&
	test_cummit C &&
	test_cummit D &&
	but branch -f main B &&
	but branch -f other &&
	but checkout other &&
	but checkout main &&
	but merge @{-1} &&
	but cat-file commit HEAD | grep "Merge branch '\''other'\''"
'

test_expect_success 'merge @{-1}~1' '
	but checkout main &&
	but reset --hard B &&
	but checkout other &&
	but checkout main &&
	but merge @{-1}~1 &&
	but cat-file commit HEAD >actual &&
	grep "Merge branch '\''other'\''" actual
'

test_expect_success 'merge @{-100} before checking out that many branches yet' '
	but reflog expire --expire=now &&
	but checkout -f main &&
	but reset --hard B &&
	but branch -f other C &&
	but checkout other &&
	but checkout main &&
	test_must_fail but merge @{-100}
'

test_expect_success 'log -g @{-1}' '
	but checkout -b last_branch &&
	but checkout -b new_branch &&
	echo "last_branch@{0}" >expect &&
	but log -g --format=%gd @{-1} >actual &&
	test_cmp expect actual
'

test_done

