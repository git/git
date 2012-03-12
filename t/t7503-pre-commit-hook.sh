#!/bin/sh

test_description='pre-commit hook'

. ./test-lib.sh

test_expect_success 'with no hook' '

	echo "foo" > file &&
	git add file &&
	git commit -m "first"

'

test_expect_success '--no-verify with no hook' '

	echo "bar" > file &&
	git add file &&
	git commit --no-verify -m "bar"

'

# now install hook that always succeeds
HOOKDIR="$(git rev-parse --git-dir)/hooks"
HOOK="$HOOKDIR/pre-commit"
mkdir -p "$HOOKDIR"
cat > "$HOOK" <<EOF
#!/bin/sh
exit 0
EOF
chmod +x "$HOOK"

test_expect_success 'with succeeding hook' '

	echo "more" >> file &&
	git add file &&
	git commit -m "more"

'

test_expect_success '--no-verify with succeeding hook' '

	echo "even more" >> file &&
	git add file &&
	git commit --no-verify -m "even more"

'

# now a hook that fails
cat > "$HOOK" <<EOF
#!/bin/sh
exit 1
EOF

test_expect_success 'with failing hook' '

	echo "another" >> file &&
	git add file &&
	test_must_fail git commit -m "another"

'

test_expect_success '--no-verify with failing hook' '

	echo "stuff" >> file &&
	git add file &&
	git commit --no-verify -m "stuff"

'

chmod -x "$HOOK"
test_expect_success POSIXPERM 'with non-executable hook' '

	echo "content" >> file &&
	git add file &&
	git commit -m "content"

'

test_expect_success POSIXPERM '--no-verify with non-executable hook' '

	echo "more content" >> file &&
	git add file &&
	git commit --no-verify -m "more content"

'
chmod +x "$HOOK"

# a hook that checks $GIT_PREFIX and succeeds inside the
# success/ subdirectory only
cat > "$HOOK" <<EOF
#!/bin/sh
test \$GIT_PREFIX = success/
EOF

test_expect_success 'with hook requiring GIT_PREFIX' '

	echo "more content" >> file &&
	git add file &&
	mkdir success &&
	(
		cd success &&
		git commit -m "hook requires GIT_PREFIX = success/"
	) &&
	rmdir success
'

test_expect_success 'with failing hook requiring GIT_PREFIX' '

	echo "more content" >> file &&
	git add file &&
	mkdir fail &&
	(
		cd fail &&
		test_must_fail git commit -m "hook must fail"
	) &&
	rmdir fail &&
	git checkout -- file
'

test_expect_success 'check the author in hook' '
	write_script "$HOOK" <<-\EOF &&
	test "$GIT_AUTHOR_NAME" = "New Author" &&
	test "$GIT_AUTHOR_EMAIL" = "newauthor@example.com"
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
	git show -s
'

test_done
