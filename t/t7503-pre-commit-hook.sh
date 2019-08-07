#!/bin/sh

test_description='pre-commit hook'

. ./test-lib.sh

HOOKDIR="$(git rev-parse --git-dir)/hooks"
PRECOMMIT="$HOOKDIR/pre-commit"

# Prepare sample scripts that write their $0 to actual_hooks
test_expect_success 'sample script setup' '
	mkdir -p "$HOOKDIR" &&
	write_script "$HOOKDIR/success.sample" <<-\EOF &&
	echo $0 >>actual_hooks
	exit 0
	EOF
	write_script "$HOOKDIR/fail.sample" <<-\EOF &&
	echo $0 >>actual_hooks
	exit 1
	EOF
	write_script "$HOOKDIR/non-exec.sample" <<-\EOF &&
	echo $0 >>actual_hooks
	exit 1
	EOF
	chmod -x "$HOOKDIR/non-exec.sample" &&
	write_script "$HOOKDIR/require-prefix.sample" <<-\EOF &&
	echo $0 >>actual_hooks
	test $GIT_PREFIX = "success/"
	EOF
	write_script "$HOOKDIR/check-author.sample" <<-\EOF
	echo $0 >>actual_hooks
	test "$GIT_AUTHOR_NAME" = "New Author" &&
	test "$GIT_AUTHOR_EMAIL" = "newauthor@example.com"
	EOF
'

test_expect_success 'with no hook' '
	test_when_finished "rm -f actual_hooks" &&
	echo "foo" >file &&
	git add file &&
	git commit -m "first" &&
	test_path_is_missing actual_hooks
'

test_expect_success '--no-verify with no hook' '
	test_when_finished "rm -f actual_hooks" &&
	echo "bar" >file &&
	git add file &&
	git commit --no-verify -m "bar" &&
	test_path_is_missing actual_hooks
'

test_expect_success 'with succeeding hook' '
	test_when_finished "rm -f \"$PRECOMMIT\" expected_hooks actual_hooks" &&
	cp "$HOOKDIR/success.sample" "$PRECOMMIT" &&
	echo "$PRECOMMIT" >expected_hooks &&
	echo "more" >>file &&
	git add file &&
	git commit -m "more" &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success '--no-verify with succeeding hook' '
	test_when_finished "rm -f \"$PRECOMMIT\" actual_hooks" &&
	cp "$HOOKDIR/success.sample" "$PRECOMMIT" &&
	echo "even more" >>file &&
	git add file &&
	git commit --no-verify -m "even more" &&
	test_path_is_missing actual_hooks
'

test_expect_success 'with failing hook' '
	test_when_finished "rm -f \"$PRECOMMIT\" expected_hooks actual_hooks" &&
	cp "$HOOKDIR/fail.sample" "$PRECOMMIT" &&
	echo "$PRECOMMIT" >expected_hooks &&
	echo "another" >>file &&
	git add file &&
	test_must_fail git commit -m "another" &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success '--no-verify with failing hook' '
	test_when_finished "rm -f \"$PRECOMMIT\" actual_hooks" &&
	cp "$HOOKDIR/fail.sample" "$PRECOMMIT" &&
	echo "stuff" >>file &&
	git add file &&
	git commit --no-verify -m "stuff" &&
	test_path_is_missing actual_hooks
'

test_expect_success POSIXPERM 'with non-executable hook' '
	test_when_finished "rm -f \"$PRECOMMIT\" actual_hooks" &&
	cp "$HOOKDIR/non-exec.sample" "$PRECOMMIT" &&
	echo "content" >>file &&
	git add file &&
	git commit -m "content" &&
	test_path_is_missing actual_hooks
'

test_expect_success POSIXPERM '--no-verify with non-executable hook' '
	test_when_finished "rm -f \"$PRECOMMIT\" actual_hooks" &&
	cp "$HOOKDIR/non-exec.sample" "$PRECOMMIT" &&
	echo "more content" >>file &&
	git add file &&
	git commit --no-verify -m "more content" &&
	test_path_is_missing actual_hooks
'

test_expect_success 'with hook requiring GIT_PREFIX' '
	test_when_finished "rm -rf \"$PRECOMMIT\" expected_hooks actual_hooks success" &&
	cp "$HOOKDIR/require-prefix.sample" "$PRECOMMIT" &&
	echo "$PRECOMMIT" >expected_hooks &&
	echo "more content" >>file &&
	git add file &&
	mkdir success &&
	(
		cd success &&
		git commit -m "hook requires GIT_PREFIX = success/"
	) &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success 'with failing hook requiring GIT_PREFIX' '
	test_when_finished "rm -rf \"$PRECOMMIT\" expected_hooks actual_hooks fail" &&
	cp "$HOOKDIR/require-prefix.sample" "$PRECOMMIT" &&
	echo "$PRECOMMIT" >expected_hooks &&
	echo "more content" >>file &&
	git add file &&
	mkdir fail &&
	(
		cd fail &&
		test_must_fail git commit -m "hook must fail"
	) &&
	git checkout -- file &&
	test_cmp expected_hooks actual_hooks
'

test_expect_success 'check the author in hook' '
	test_when_finished "rm -f \"$PRECOMMIT\" expected_hooks actual_hooks" &&
	cp "$HOOKDIR/check-author.sample" "$PRECOMMIT" &&
	cat >expected_hooks <<-EOF &&
	$PRECOMMIT
	$PRECOMMIT
	$PRECOMMIT
	EOF
	test_must_fail git commit --allow-empty -m "by a.u.thor" &&
	(
		GIT_AUTHOR_NAME="New Author" &&
		GIT_AUTHOR_EMAIL="newauthor@example.com" &&
		export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL &&
		git commit --allow-empty -m "by new.author via env" &&
		git show -s
	) &&
	git commit --author="New Author <newauthor@example.com>" \
		--allow-empty -m "by new.author via command line" &&
	git show -s &&
	test_cmp expected_hooks actual_hooks
'

test_done
