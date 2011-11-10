#!/bin/sh

test_description='git rebase --abort tests'

. ./test-lib.sh

### Test that we handle space characters properly
work_dir="$(pwd)/test dir"

test_expect_success setup '
	mkdir -p "$work_dir" &&
	cd "$work_dir" &&
	git init &&
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
		cd "$work_dir" &&
		# Clean up the state from the previous one
		git reset --hard pre-rebase &&
		test_must_fail git rebase$type master &&
		test -d "$dotest" &&
		git rebase --abort &&
		test $(git rev-parse to-rebase) = $(git rev-parse pre-rebase) &&
		test ! -d "$dotest"
	'

	test_expect_success "rebase$type --abort after --skip" '
		cd "$work_dir" &&
		# Clean up the state from the previous one
		git reset --hard pre-rebase &&
		test_must_fail git rebase$type master &&
		test -d "$dotest" &&
		test_must_fail git rebase --skip &&
		test $(git rev-parse HEAD) = $(git rev-parse master) &&
		git rebase --abort &&
		test $(git rev-parse to-rebase) = $(git rev-parse pre-rebase) &&
		test ! -d "$dotest"
	'

	test_expect_success "rebase$type --abort after --continue" '
		cd "$work_dir" &&
		# Clean up the state from the previous one
		git reset --hard pre-rebase &&
		test_must_fail git rebase$type master &&
		test -d "$dotest" &&
		echo c > a &&
		echo d >> a &&
		git add a &&
		test_must_fail git rebase --continue &&
		test $(git rev-parse HEAD) != $(git rev-parse master) &&
		git rebase --abort &&
		test $(git rev-parse to-rebase) = $(git rev-parse pre-rebase) &&
		test ! -d "$dotest"
	'
}

testrebase "" .git/rebase-apply
testrebase " --merge" .git/rebase-merge

test_done
