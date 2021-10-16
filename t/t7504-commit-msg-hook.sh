#!/bin/sh

test_description='commit-msg hook'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'with no hook' '

	echo "foo" > file &&
	git add file &&
	git commit -m "first"

'

# set up fake editor for interactive editing
cat > fake-editor <<'EOF'
#!/bin/sh
cp FAKE_MSG "$1"
exit 0
EOF
chmod +x fake-editor

## Not using test_set_editor here so we can easily ensure the editor variable
## is only set for the editor tests
FAKE_EDITOR="$(pwd)/fake-editor"
export FAKE_EDITOR

test_expect_success 'with no hook (editor)' '

	echo "more foo" >> file &&
	git add file &&
	echo "more foo" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit

'

test_expect_success '--no-verify with no hook' '

	echo "bar" > file &&
	git add file &&
	git commit --no-verify -m "bar"

'

test_expect_success '--no-verify with no hook (editor)' '

	echo "more bar" > file &&
	git add file &&
	echo "more bar" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit --no-verify

'

# now install hook that always succeeds
HOOKDIR="$(git rev-parse --git-dir)/hooks"
HOOK="$HOOKDIR/commit-msg"
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

test_expect_success 'with succeeding hook (editor)' '

	echo "more more" >> file &&
	git add file &&
	echo "more more" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit

'

test_expect_success '--no-verify with succeeding hook' '

	echo "even more" >> file &&
	git add file &&
	git commit --no-verify -m "even more"

'

test_expect_success '--no-verify with succeeding hook (editor)' '

	echo "even more more" >> file &&
	git add file &&
	echo "even more more" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit --no-verify

'

# now a hook that fails
cat > "$HOOK" <<EOF
#!/bin/sh
exit 1
EOF

commit_msg_is () {
	test "$(git log --pretty=format:%s%b -1)" = "$1"
}

test_expect_success 'with failing hook' '

	echo "another" >> file &&
	git add file &&
	test_must_fail git commit -m "another"

'

test_expect_success 'with failing hook (editor)' '

	echo "more another" >> file &&
	git add file &&
	echo "more another" > FAKE_MSG &&
	! (GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit)

'

test_expect_success '--no-verify with failing hook' '

	echo "stuff" >> file &&
	git add file &&
	git commit --no-verify -m "stuff"

'

test_expect_success '--no-verify with failing hook (editor)' '

	echo "more stuff" >> file &&
	git add file &&
	echo "more stuff" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit --no-verify

'

test_expect_success 'merge fails with failing hook' '

	test_when_finished "git branch -D newbranch" &&
	test_when_finished "git checkout -f main" &&
	git checkout --orphan newbranch &&
	: >file2 &&
	git add file2 &&
	git commit --no-verify file2 -m in-side-branch &&
	test_must_fail git merge --allow-unrelated-histories main &&
	commit_msg_is "in-side-branch" # HEAD before merge

'

test_expect_success 'merge bypasses failing hook with --no-verify' '

	test_when_finished "git branch -D newbranch" &&
	test_when_finished "git checkout -f main" &&
	git checkout --orphan newbranch &&
	git rm -f file &&
	: >file2 &&
	git add file2 &&
	git commit --no-verify file2 -m in-side-branch &&
	git merge --no-verify --allow-unrelated-histories main &&
	commit_msg_is "Merge branch '\''main'\'' into newbranch"
'


chmod -x "$HOOK"
test_expect_success POSIXPERM 'with non-executable hook' '

	echo "content" >file &&
	git add file &&
	git commit -m "content"

'

test_expect_success POSIXPERM 'with non-executable hook (editor)' '

	echo "content again" >> file &&
	git add file &&
	echo "content again" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit -m "content again"

'

test_expect_success POSIXPERM '--no-verify with non-executable hook' '

	echo "more content" >> file &&
	git add file &&
	git commit --no-verify -m "more content"

'

test_expect_success POSIXPERM '--no-verify with non-executable hook (editor)' '

	echo "even more content" >> file &&
	git add file &&
	echo "even more content" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit --no-verify

'

# now a hook that edits the commit message
cat > "$HOOK" <<'EOF'
#!/bin/sh
echo "new message" > "$1"
exit 0
EOF
chmod +x "$HOOK"

test_expect_success 'hook edits commit message' '

	echo "additional" >> file &&
	git add file &&
	git commit -m "additional" &&
	commit_msg_is "new message"

'

test_expect_success 'hook edits commit message (editor)' '

	echo "additional content" >> file &&
	git add file &&
	echo "additional content" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit &&
	commit_msg_is "new message"

'

test_expect_success "hook doesn't edit commit message" '

	echo "plus" >> file &&
	git add file &&
	git commit --no-verify -m "plus" &&
	commit_msg_is "plus"

'

test_expect_success "hook doesn't edit commit message (editor)" '

	echo "more plus" >> file &&
	git add file &&
	echo "more plus" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit --no-verify &&
	commit_msg_is "more plus"
'

test_expect_success 'hook called in git-merge picks up commit message' '
	test_when_finished "git branch -D newbranch" &&
	test_when_finished "git checkout -f main" &&
	git checkout --orphan newbranch &&
	git rm -f file &&
	: >file2 &&
	git add file2 &&
	git commit --no-verify file2 -m in-side-branch &&
	git merge --allow-unrelated-histories main &&
	commit_msg_is "new message"
'

test_expect_failure 'merge --continue remembers --no-verify' '
	test_when_finished "git branch -D newbranch" &&
	test_when_finished "git checkout -f main" &&
	git checkout main &&
	echo a >file2 &&
	git add file2 &&
	git commit --no-verify -m "add file2 to main" &&
	git checkout -b newbranch main^ &&
	echo b >file2 &&
	git add file2 &&
	git commit --no-verify file2 -m in-side-branch &&
	git merge --no-verify -m not-rewritten-by-hook main &&
	# resolve conflict:
	echo c >file2 &&
	git add file2 &&
	git merge --continue &&
	commit_msg_is not-rewritten-by-hook
'

# set up fake editor to replace `pick` by `reword`
cat > reword-editor <<'EOF'
#!/bin/sh
mv "$1" "$1".bup &&
sed 's/^pick/reword/' <"$1".bup >"$1"
EOF
chmod +x reword-editor
REWORD_EDITOR="$(pwd)/reword-editor"
export REWORD_EDITOR

test_expect_success 'hook is called for reword during `rebase -i`' '

	GIT_SEQUENCE_EDITOR="\"$REWORD_EDITOR\"" git rebase -i HEAD^ &&
	commit_msg_is "new message"

'


test_done
