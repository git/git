#!/bin/sh

test_description='git merge and other operations that rely on merge

Testing the influence of the diff algorithm on the merge output.'

. ./test-lib.sh

test_expect_success 'setup' '
	cp "$TEST_DIRECTORY"/t7615/base.c file.c &&
	git add file.c &&
	git commit -m c0 &&
	git tag c0 &&
	cp "$TEST_DIRECTORY"/t7615/ours.c file.c &&
	git add file.c &&
	git commit -m c1 &&
	git tag c1 &&
	git reset --hard c0 &&
	cp "$TEST_DIRECTORY"/t7615/theirs.c file.c &&
	git add file.c &&
	git commit -m c2 &&
	git tag c2
'

GIT_TEST_MERGE_ALGORITHM=recursive

test_expect_success 'merge c2 to c1 with recursive merge strategy fails with the current default myers diff algorithm' '
	git reset --hard c1 &&
	test_must_fail git merge -s recursive c2
'

test_expect_success 'merge c2 to c1 with recursive merge strategy succeeds with -Xdiff-algorithm=histogram' '
	git reset --hard c1 &&
	git merge --strategy recursive -Xdiff-algorithm=histogram c2
'

test_expect_success 'merge c2 to c1 with recursive merge strategy succeeds with diff.algorithm = histogram' '
	git reset --hard c1 &&
	git config diff.algorithm histogram &&
	git merge --strategy recursive c2
'

test_expect_success 'cherry-pick c2 to c1 with recursive merge strategy fails with the current default myers diff algorithm' '
	git reset --hard c1 &&
	test_must_fail git cherry-pick -s recursive c2
'

test_expect_success 'cherry-pick c2 to c1 with recursive merge strategy succeeds with -Xdiff-algorithm=histogram' '
	git reset --hard c1 &&
	git cherry-pick --strategy recursive -Xdiff-algorithm=histogram c2
'

test_expect_success 'cherry-pick c2 to c1 with recursive merge strategy succeeds with diff.algorithm = histogram' '
	git reset --hard c1 &&
	git config diff.algorithm histogram &&
	git cherry-pick --strategy recursive c2
'

test_done
