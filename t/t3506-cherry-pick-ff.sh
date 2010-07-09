#!/bin/sh

test_description='test cherry-picking with --ff option'

. ./test-lib.sh

test_expect_success setup '
	echo first > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m "first" &&
	git tag first &&

	git checkout -b other &&
	echo second >> file1 &&
	git add file1 &&
	test_tick &&
	git commit -m "second" &&
	git tag second
'

test_expect_success 'cherry-pick using --ff fast forwards' '
	git checkout master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick --ff second &&
	test "$(git rev-parse --verify HEAD)" = "$(git rev-parse --verify second)"
'

test_expect_success 'cherry-pick not using --ff does not fast forwards' '
	git checkout master &&
	git reset --hard first &&
	test_tick &&
	git cherry-pick second &&
	test "$(git rev-parse --verify HEAD)" != "$(git rev-parse --verify second)"
'

#
# We setup the following graph:
#
#	      B---C
#	     /   /
#	first---A
#
# (This has been taken from t3502-cherry-pick-merge.sh)
#
test_expect_success 'merge setup' '
	git checkout master &&
	git reset --hard first &&
	echo new line >A &&
	git add A &&
	test_tick &&
	git commit -m "add line to A" A &&
	git tag A &&
	git checkout -b side first &&
	echo new line >B &&
	git add B &&
	test_tick &&
	git commit -m "add line to B" B &&
	git tag B &&
	git checkout master &&
	git merge side &&
	git tag C &&
	git checkout -b new A
'

test_expect_success 'cherry-pick a non-merge with --ff and -m should fail' '
	git reset --hard A -- &&
	test_must_fail git cherry-pick --ff -m 1 B &&
	git diff --exit-code A --
'

test_expect_success 'cherry pick a merge with --ff but without -m should fail' '
	git reset --hard A -- &&
	test_must_fail git cherry-pick --ff C &&
	git diff --exit-code A --
'

test_expect_success 'cherry pick with --ff a merge (1)' '
	git reset --hard A -- &&
	git cherry-pick --ff -m 1 C &&
	git diff --exit-code C &&
	test "$(git rev-parse --verify HEAD)" = "$(git rev-parse --verify C)"
'

test_expect_success 'cherry pick with --ff a merge (2)' '
	git reset --hard B -- &&
	git cherry-pick --ff -m 2 C &&
	git diff --exit-code C &&
	test "$(git rev-parse --verify HEAD)" = "$(git rev-parse --verify C)"
'

test_expect_success 'cherry pick a merge relative to nonexistent parent with --ff should fail' '
	git reset --hard B -- &&
	test_must_fail git cherry-pick --ff -m 3 C
'

test_done
