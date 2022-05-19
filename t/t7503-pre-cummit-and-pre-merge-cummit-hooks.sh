#!/bin/sh

test_description='pre-cummit and pre-merge-commit hooks'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'root cummit' '
	echo "root" >file &&
	git add file &&
	git cummit -m "zeroth" &&
	git checkout -b side &&
	echo "foo" >foo &&
	git add foo &&
	git cummit -m "make it non-ff" &&
	git branch side-orig side &&
	git checkout main
'

test_expect_success 'setup conflicting branches' '
	test_when_finished "git checkout main" &&
	git checkout -b conflicting-a main &&
	echo a >conflicting &&
	git add conflicting &&
	git cummit -m conflicting-a &&
	git checkout -b conflicting-b main &&
	echo b >conflicting &&
	git add conflicting &&
	git cummit -m conflicting-b
'

test_expect_success 'with no hook' '
	test_when_finished "rm -f actual_hooks" &&
	echo "foo" >file &&
	git add file &&
	git cummit -m "first" &&
	test_path_is_missing actual_hooks
'

test_expect_success 'with no hook (merge)' '
	test_when_finished "rm -f actual_hooks" &&
	git branch -f side side-orig &&
	git checkout side &&
	git merge -m "merge main" main &&
	git checkout main &&
	test_path_is_missing actual_hooks
'

test_expect_success '--no-verify with no hook' '
	test_when_finished "rm -f actual_hooks" &&
	echo "bar" >file &&
	git add file &&
	git cummit --no-verify -m "bar" &&
	test_path_is_missing actual_hooks
'

test_expect_success '--no-verify with no hook (merge)' '
	test_when_finished "rm -f actual_hooks" &&
	git branch -f side side-orig &&
	git checkout side &&
	git merge --no-verify -m "merge main" main &&
	git checkout main &&
	test_path_is_missing actual_hooks
'

setup_success_hook () {
	test_when_finished "rm -f actual_hooks expected_hooks" &&
	echo "$1" >expected_hooks &&
	test_hook "$1" <<-EOF
	echo $1 >>actual_hooks
	EOF
}

test_expect_success 'with succeeding hook' '
	setup_success_hook "pre-cummit" &&
	echo "more" >>file &&
	git add file &&
	git cummit -m "more" &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success 'with succeeding hook (merge)' '
	setup_success_hook "pre-merge-cummit" &&
	git checkout side &&
	git merge -m "merge main" main &&
	git checkout main &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success 'automatic merge fails; both hooks are available' '
	setup_success_hook "pre-cummit" &&
	setup_success_hook "pre-merge-cummit" &&

	git checkout conflicting-a &&
	test_must_fail git merge -m "merge conflicting-b" conflicting-b &&
	test_path_is_missing actual_hooks &&

	echo "pre-cummit" >expected_hooks &&
	echo a+b >conflicting &&
	git add conflicting &&
	git cummit -m "resolve conflict" &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success '--no-verify with succeeding hook' '
	setup_success_hook "pre-cummit" &&
	echo "even more" >>file &&
	git add file &&
	git cummit --no-verify -m "even more" &&
	test_path_is_missing actual_hooks
'

test_expect_success '--no-verify with succeeding hook (merge)' '
	setup_success_hook "pre-merge-cummit" &&
	git branch -f side side-orig &&
	git checkout side &&
	git merge --no-verify -m "merge main" main &&
	git checkout main &&
	test_path_is_missing actual_hooks
'

setup_failing_hook () {
	test_when_finished "rm -f actual_hooks" &&
	test_hook "$1" <<-EOF
	echo $1-failing-hook >>actual_hooks
	exit 1
	EOF
}

test_expect_success 'with failing hook' '
	setup_failing_hook "pre-cummit" &&
	test_when_finished "rm -f expected_hooks" &&
	echo "pre-cummit-failing-hook" >expected_hooks &&

	echo "another" >>file &&
	git add file &&
	test_must_fail git cummit -m "another" &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success '--no-verify with failing hook' '
	setup_failing_hook "pre-cummit" &&
	echo "stuff" >>file &&
	git add file &&
	git cummit --no-verify -m "stuff" &&
	test_path_is_missing actual_hooks
'

test_expect_success 'with failing hook (merge)' '
	setup_failing_hook "pre-merge-cummit" &&
	echo "pre-merge-cummit-failing-hook" >expected_hooks &&
	git checkout side &&
	test_must_fail git merge -m "merge main" main &&
	git checkout main &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success '--no-verify with failing hook (merge)' '
	setup_failing_hook "pre-merge-cummit" &&

	git branch -f side side-orig &&
	git checkout side &&
	git merge --no-verify -m "merge main" main &&
	git checkout main &&
	test_path_is_missing actual_hooks
'

setup_non_exec_hook () {
	test_when_finished "rm -f actual_hooks" &&
	test_hook "$1" <<-\EOF &&
	echo non-exec >>actual_hooks
	exit 1
	EOF
	test_hook --disable "$1"
}


test_expect_success POSIXPERM 'with non-executable hook' '
	setup_non_exec_hook "pre-cummit" &&
	echo "content" >>file &&
	git add file &&
	git cummit -m "content" &&
	test_path_is_missing actual_hooks
'

test_expect_success POSIXPERM '--no-verify with non-executable hook' '
	setup_non_exec_hook "pre-cummit" &&
	echo "more content" >>file &&
	git add file &&
	git cummit --no-verify -m "more content" &&
	test_path_is_missing actual_hooks
'

test_expect_success POSIXPERM 'with non-executable hook (merge)' '
	setup_non_exec_hook "pre-merge" &&
	git branch -f side side-orig &&
	git checkout side &&
	git merge -m "merge main" main &&
	git checkout main &&
	test_path_is_missing actual_hooks
'

test_expect_success POSIXPERM '--no-verify with non-executable hook (merge)' '
	setup_non_exec_hook "pre-merge" &&
	git branch -f side side-orig &&
	git checkout side &&
	git merge --no-verify -m "merge main" main &&
	git checkout main &&
	test_path_is_missing actual_hooks
'

setup_require_prefix_hook () {
	test_when_finished "rm -f expected_hooks" &&
	echo require-prefix >expected_hooks &&
	test_hook pre-cummit <<-\EOF
	echo require-prefix >>actual_hooks
	test $GIT_PREFIX = "success/"
	EOF
}

test_expect_success 'with hook requiring GIT_PREFIX' '
	test_when_finished "rm -rf actual_hooks success" &&
	setup_require_prefix_hook &&
	echo "more content" >>file &&
	git add file &&
	mkdir success &&
	(
		cd success &&
		git cummit -m "hook requires GIT_PREFIX = success/"
	) &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success 'with failing hook requiring GIT_PREFIX' '
	test_when_finished "rm -rf actual_hooks fail" &&
	setup_require_prefix_hook &&
	echo "more content" >>file &&
	git add file &&
	mkdir fail &&
	(
		cd fail &&
		test_must_fail git cummit -m "hook must fail"
	) &&
	git checkout -- file &&
	test_cmp expected_hooks actual_hooks
'

setup_require_author_hook () {
	test_when_finished "rm -f expected_hooks actual_hooks" &&
	echo check-author >expected_hooks &&
	test_hook pre-cummit <<-\EOF
	echo check-author >>actual_hooks
	test "$GIT_AUTHOR_NAME" = "New Author" &&
	test "$GIT_AUTHOR_EMAIL" = "newauthor@example.com"
	EOF
}


test_expect_success 'check the author in hook' '
	setup_require_author_hook &&
	cat >expected_hooks <<-EOF &&
	check-author
	check-author
	check-author
	EOF
	test_must_fail git cummit --allow-empty -m "by a.u.thor" &&
	(
		GIT_AUTHOR_NAME="New Author" &&
		GIT_AUTHOR_EMAIL="newauthor@example.com" &&
		export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL &&
		git cummit --allow-empty -m "by new.author via env" &&
		git show -s
	) &&
	git cummit --author="New Author <newauthor@example.com>" \
		--allow-empty -m "by new.author via command line" &&
	git show -s &&
	test_cmp expected_hooks actual_hooks
'

test_done
