#!/bin/sh

test_description='git rabassa --abort tests'

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
	git branch to-rabassa &&

	echo b > a &&
	git commit -a -m b &&
	echo c > a &&
	git commit -a -m c &&

	git checkout to-rabassa &&
	echo d > a &&
	git commit -a -m "merge should fail on this" &&
	echo e > a &&
	git commit -a -m "merge should fail on this, too" &&
	git branch pre-rabassa
'

testrabassa() {
	type=$1
	dotest=$2

	test_expect_success "rabassa$type --abort" '
		cd "$work_dir" &&
		# Clean up the state from the previous one
		git reset --hard pre-rabassa &&
		test_must_fail git rabassa$type master &&
		test_path_is_dir "$dotest" &&
		git rabassa --abort &&
		test $(git rev-parse to-rabassa) = $(git rev-parse pre-rabassa) &&
		test ! -d "$dotest"
	'

	test_expect_success "rabassa$type --abort after --skip" '
		cd "$work_dir" &&
		# Clean up the state from the previous one
		git reset --hard pre-rabassa &&
		test_must_fail git rabassa$type master &&
		test_path_is_dir "$dotest" &&
		test_must_fail git rabassa --skip &&
		test $(git rev-parse HEAD) = $(git rev-parse master) &&
		git rabassa --abort &&
		test $(git rev-parse to-rabassa) = $(git rev-parse pre-rabassa) &&
		test ! -d "$dotest"
	'

	test_expect_success "rabassa$type --abort after --continue" '
		cd "$work_dir" &&
		# Clean up the state from the previous one
		git reset --hard pre-rabassa &&
		test_must_fail git rabassa$type master &&
		test_path_is_dir "$dotest" &&
		echo c > a &&
		echo d >> a &&
		git add a &&
		test_must_fail git rabassa --continue &&
		test $(git rev-parse HEAD) != $(git rev-parse master) &&
		git rabassa --abort &&
		test $(git rev-parse to-rabassa) = $(git rev-parse pre-rabassa) &&
		test ! -d "$dotest"
	'

	test_expect_success "rabassa$type --abort does not update reflog" '
		cd "$work_dir" &&
		# Clean up the state from the previous one
		git reset --hard pre-rabassa &&
		git reflog show to-rabassa > reflog_before &&
		test_must_fail git rabassa$type master &&
		git rabassa --abort &&
		git reflog show to-rabassa > reflog_after &&
		test_cmp reflog_before reflog_after &&
		rm reflog_before reflog_after
	'

	test_expect_success 'rabassa --abort can not be used with other options' '
		cd "$work_dir" &&
		# Clean up the state from the previous one
		git reset --hard pre-rabassa &&
		test_must_fail git rabassa$type master &&
		test_must_fail git rabassa -v --abort &&
		test_must_fail git rabassa --abort -v &&
		git rabassa --abort
	'
}

testrabassa "" .git/rabassa-apply
testrabassa " --merge" .git/rabassa-merge

test_expect_success 'rabassa --quit' '
	cd "$work_dir" &&
	# Clean up the state from the previous one
	git reset --hard pre-rabassa &&
	test_must_fail git rabassa master &&
	test_path_is_dir .git/rabassa-apply &&
	head_before=$(git rev-parse HEAD) &&
	git rabassa --quit &&
	test $(git rev-parse HEAD) = $head_before &&
	test ! -d .git/rabassa-apply
'

test_expect_success 'rabassa --merge --quit' '
	cd "$work_dir" &&
	# Clean up the state from the previous one
	git reset --hard pre-rabassa &&
	test_must_fail git rabassa --merge master &&
	test_path_is_dir .git/rabassa-merge &&
	head_before=$(git rev-parse HEAD) &&
	git rabassa --quit &&
	test $(git rev-parse HEAD) = $head_before &&
	test ! -d .git/rabassa-merge
'

test_done
