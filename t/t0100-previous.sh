#!/bin/sh

test_description='previous branch syntax @{-n}'

. ./test-lib.sh

test_expect_success 'branch -d @{-1}' '
	test_commit A &&
	git checkout -b junk &&
	git checkout - &&
	test "$(git symbolic-ref HEAD)" = refs/heads/master &&
	git branch -d @{-1} &&
	test_must_fail git rev-parse --verify refs/heads/junk
'

test_expect_success 'branch -d @{-12} when there is not enough switches yet' '
	git reflog expire --expire=now &&
	git checkout -b junk2 &&
	git checkout - &&
	test "$(git symbolic-ref HEAD)" = refs/heads/master &&
	test_must_fail git branch -d @{-12} &&
	git rev-parse --verify refs/heads/master
'

test_expect_success 'merge @{-1}' '
	git checkout A &&
	test_commit B &&
	git checkout A &&
	test_commit C &&
	git branch -f master B &&
	git branch -f other &&
	git checkout other &&
	git checkout master &&
	git merge @{-1} &&
	git cat-file commit HEAD | grep "Merge branch '\''other'\''"
'

test_expect_success 'merge @{-1} when there is not enough switches yet' '
	git reflog expire --expire=now &&
	git checkout -f master &&
	git reset --hard B &&
	git branch -f other C &&
	git checkout other &&
	git checkout master &&
	test_must_fail git merge @{-12}
'

test_done

