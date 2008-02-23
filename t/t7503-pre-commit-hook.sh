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
	! git commit -m "another"

'

test_expect_success '--no-verify with failing hook' '

	echo "stuff" >> file &&
	git add file &&
	git commit --no-verify -m "stuff"

'

chmod -x "$HOOK"
test_expect_success 'with non-executable hook' '

	echo "content" >> file &&
	git add file &&
	git commit -m "content"

'

test_expect_success '--no-verify with non-executable hook' '

	echo "more content" >> file &&
	git add file &&
	git commit --no-verify -m "more content"

'

test_done
