#!/bin/sh
#
# Copyright (c) 2010 Erick Mattos
#

test_description='git checkout --orphan

Main Tests for --orphan functionality.'

. ./test-lib.sh

TEST_FILE=foo

test_expect_success 'Setup' '
	echo "Initial" >"$TEST_FILE" &&
	git add "$TEST_FILE" &&
	git commit -m "First Commit"
	test_tick &&
	echo "State 1" >>"$TEST_FILE" &&
	git add "$TEST_FILE" &&
	test_tick &&
	git commit -m "Second Commit"
'

test_expect_success '--orphan creates a new orphan branch from HEAD' '
	git checkout --orphan alpha &&
	test_must_fail git rev-parse --verify HEAD &&
	test "refs/heads/alpha" = "$(git symbolic-ref HEAD)" &&
	test_tick &&
	git commit -m "Third Commit" &&
	test_must_fail git rev-parse --verify HEAD^ &&
	git diff-tree --quiet master alpha
'

test_expect_success '--orphan creates a new orphan branch from <start_point>' '
	git checkout master &&
	git checkout --orphan beta master^ &&
	test_must_fail git rev-parse --verify HEAD &&
	test "refs/heads/beta" = "$(git symbolic-ref HEAD)" &&
	test_tick &&
	git commit -m "Fourth Commit" &&
	test_must_fail git rev-parse --verify HEAD^ &&
	git diff-tree --quiet master^ beta
'

test_expect_success '--orphan must be rejected with -b' '
	git checkout master &&
	test_must_fail git checkout --orphan new -b newer &&
	test refs/heads/master = "$(git symbolic-ref HEAD)"
'

test_expect_success '--orphan is rejected with an existing name' '
	git checkout master &&
	test_must_fail git checkout --orphan master &&
	test refs/heads/master = "$(git symbolic-ref HEAD)"
'

test_expect_success '--orphan refuses to switch if a merge is needed' '
	git checkout master &&
	git reset --hard &&
	echo local >>"$TEST_FILE" &&
	cat "$TEST_FILE" >"$TEST_FILE.saved" &&
	test_must_fail git checkout --orphan gamma master^ &&
	test refs/heads/master = "$(git symbolic-ref HEAD)" &&
	test_cmp "$TEST_FILE" "$TEST_FILE.saved" &&
	git diff-index --quiet --cached HEAD &&
	git reset --hard
'

test_expect_success '--orphan does not mix well with -t' '
	git checkout master &&
	test_must_fail git checkout -t master --orphan gamma &&
	test refs/heads/master = "$(git symbolic-ref HEAD)"
'

test_expect_success '--orphan ignores branch.autosetupmerge' '
	git checkout -f master &&
	git config branch.autosetupmerge always &&
	git checkout --orphan delta &&
	test -z "$(git config branch.delta.merge)" &&
	test refs/heads/delta = "$(git symbolic-ref HEAD)" &&
	test_must_fail git rev-parse --verify HEAD^
'

test_expect_success '--orphan does not mix well with -l' '
	git checkout -f master &&
	test_must_fail git checkout -l --orphan gamma
'

test_done
