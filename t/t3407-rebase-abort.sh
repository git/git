#!/bin/sh

test_description='git rebase --abort tests'

. ./test-lib.sh

test_expect_success setup '
	echo a > a &&
	git add a &&
	git commit -m a &&
	git branch to-rebase &&

	echo b > a &&
	git commit -a -m b &&
	echo c > a &&
	git commit -a -m c &&

	git checkout to-rebase &&
	echo d > a &&
	git commit -a -m "merge should fail on this" &&
	echo e > a &&
	git commit -a -m "merge should fail on this, too" &&
	git branch pre-rebase
'

testrebase() {
	type=$1
	dotest=$2

	test_expect_success "rebase$type --abort" '
		# Clean up the state from the previous one
		git reset --hard pre-rebase
		test_must_fail git rebase'"$type"' master &&
		test -d '$dotest' &&
		git rebase --abort &&
		test $(git rev-parse to-rebase) = $(git rev-parse pre-rebase) &&
		test ! -d '$dotest'
	'

	test_expect_success "rebase$type --abort after --skip" '
		# Clean up the state from the previous one
		git reset --hard pre-rebase
		test_must_fail git rebase'"$type"' master &&
		test -d '$dotest' &&
		test_must_fail git rebase --skip &&
		test $(git rev-parse HEAD) = $(git rev-parse master) &&
		git-rebase --abort &&
		test $(git rev-parse to-rebase) = $(git rev-parse pre-rebase) &&
		test ! -d '$dotest'
	'

	test_expect_success "rebase$type --abort after --continue" '
		# Clean up the state from the previous one
		git reset --hard pre-rebase
		test_must_fail git rebase'"$type"' master &&
		test -d '$dotest' &&
		echo c > a &&
		echo d >> a &&
		git add a &&
		test_must_fail git rebase --continue &&
		test $(git rev-parse HEAD) != $(git rev-parse master) &&
		git rebase --abort &&
		test $(git rev-parse to-rebase) = $(git rev-parse pre-rebase) &&
		test ! -d '$dotest'
	'
}

testrebase "" .dotest
testrebase " --merge" .git/.dotest-merge

test_done
