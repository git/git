#!/bin/sh

test_description='previous branch syntax @{-n}'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'branch -d @{-1}' '
	test_commit A &&
	git checkout -b junk &&
	git checkout - &&
	echo refs/heads/main >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual &&
	git branch -d @{-1} &&
	test_must_fail git rev-parse --verify refs/heads/junk
'

test_expect_success 'branch -d @{-12} when there is not enough switches yet' '
	git reflog expire --expire=now &&
	git checkout -b junk2 &&
	git checkout - &&
	echo refs/heads/main >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual &&
	test_must_fail git branch -d @{-12} &&
	git rev-parse --verify refs/heads/main
'

test_expect_success 'merge @{-1}' '
	git checkout A &&
	test_commit B &&
	git checkout A &&
	test_commit C &&
	test_commit D &&
	git branch -f main B &&
	git branch -f other &&
	git checkout other &&
	git checkout main &&
	git merge @{-1} &&
	git cat-file commit HEAD | grep "Merge branch '\''other'\''"
'

test_expect_success 'merge @{-1}~1' '
	git checkout main &&
	git reset --hard B &&
	git checkout other &&
	git checkout main &&
	git merge @{-1}~1 &&
	git cat-file commit HEAD >actual &&
	grep "Merge branch '\''other'\''" actual
'

test_expect_success 'merge @{-100} before checking out that many branches yet' '
	git reflog expire --expire=now &&
	git checkout -f main &&
	git reset --hard B &&
	git branch -f other C &&
	git checkout other &&
	git checkout main &&
	test_must_fail git merge @{-100}
'

test_expect_success 'log -g @{-1}' '
	git checkout -b last_branch &&
	git checkout -b new_branch &&
	echo "last_branch@{0}" >expect &&
	git log -g --format=%gd @{-1} >actual &&
	test_cmp expect actual
'

test_done

