#!/bin/sh

test_description='cummit-msg hook'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'with no hook' '

	echo "foo" > file &&
	but add file &&
	but cummit -m "first"

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
	but add file &&
	echo "more foo" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit

'

test_expect_success '--no-verify with no hook' '

	echo "bar" > file &&
	but add file &&
	but cummit --no-verify -m "bar"

'

test_expect_success '--no-verify with no hook (editor)' '

	echo "more bar" > file &&
	but add file &&
	echo "more bar" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit --no-verify

'

test_expect_success 'setup: cummit-msg hook that always succeeds' '
	test_hook --setup cummit-msg <<-\EOF
	exit 0
	EOF
'

test_expect_success 'with succeeding hook' '

	echo "more" >> file &&
	but add file &&
	but cummit -m "more"

'

test_expect_success 'with succeeding hook (editor)' '

	echo "more more" >> file &&
	but add file &&
	echo "more more" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit

'

test_expect_success '--no-verify with succeeding hook' '

	echo "even more" >> file &&
	but add file &&
	but cummit --no-verify -m "even more"

'

test_expect_success '--no-verify with succeeding hook (editor)' '

	echo "even more more" >> file &&
	but add file &&
	echo "even more more" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit --no-verify

'

test_expect_success 'setup: cummit-msg hook that always fails' '
	test_hook --clobber cummit-msg <<-\EOF
	exit 1
	EOF
'

cummit_msg_is () {
	test "$(but log --pretty=format:%s%b -1)" = "$1"
}

test_expect_success 'with failing hook' '

	echo "another" >> file &&
	but add file &&
	test_must_fail but cummit -m "another"

'

test_expect_success 'with failing hook (editor)' '

	echo "more another" >> file &&
	but add file &&
	echo "more another" > FAKE_MSG &&
	! (GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit)

'

test_expect_success '--no-verify with failing hook' '

	echo "stuff" >> file &&
	but add file &&
	but cummit --no-verify -m "stuff"

'

test_expect_success '-n followed by --verify with failing hook' '

	echo "even more" >> file &&
	but add file &&
	test_must_fail but cummit -n --verify -m "even more"

'

test_expect_success '--no-verify with failing hook (editor)' '

	echo "more stuff" >> file &&
	but add file &&
	echo "more stuff" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit --no-verify

'

test_expect_success 'merge fails with failing hook' '

	test_when_finished "but branch -D newbranch" &&
	test_when_finished "but checkout -f main" &&
	but checkout --orphan newbranch &&
	: >file2 &&
	but add file2 &&
	but cummit --no-verify file2 -m in-side-branch &&
	test_must_fail but merge --allow-unrelated-histories main &&
	cummit_msg_is "in-side-branch" # HEAD before merge

'

test_expect_success 'merge bypasses failing hook with --no-verify' '

	test_when_finished "but branch -D newbranch" &&
	test_when_finished "but checkout -f main" &&
	but checkout --orphan newbranch &&
	but rm -f file &&
	: >file2 &&
	but add file2 &&
	but cummit --no-verify file2 -m in-side-branch &&
	but merge --no-verify --allow-unrelated-histories main &&
	cummit_msg_is "Merge branch '\''main'\'' into newbranch"
'

test_expect_success 'setup: cummit-msg hook made non-executable' '
	but_dir="$(but rev-parse --but-dir)" &&
	chmod -x "$but_dir/hooks/cummit-msg"
'


test_expect_success POSIXPERM 'with non-executable hook' '

	echo "content" >file &&
	but add file &&
	but cummit -m "content"

'

test_expect_success POSIXPERM 'with non-executable hook (editor)' '

	echo "content again" >> file &&
	but add file &&
	echo "content again" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit -m "content again"

'

test_expect_success POSIXPERM '--no-verify with non-executable hook' '

	echo "more content" >> file &&
	but add file &&
	but cummit --no-verify -m "more content"

'

test_expect_success POSIXPERM '--no-verify with non-executable hook (editor)' '

	echo "even more content" >> file &&
	but add file &&
	echo "even more content" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit --no-verify

'

test_expect_success 'setup: cummit-msg hook that edits the cummit message' '
	test_hook --clobber cummit-msg <<-\EOF
	echo "new message" >"$1"
	exit 0
	EOF
'

test_expect_success 'hook edits cummit message' '

	echo "additional" >> file &&
	but add file &&
	but cummit -m "additional" &&
	cummit_msg_is "new message"

'

test_expect_success 'hook edits cummit message (editor)' '

	echo "additional content" >> file &&
	but add file &&
	echo "additional content" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit &&
	cummit_msg_is "new message"

'

test_expect_success "hook doesn't edit cummit message" '

	echo "plus" >> file &&
	but add file &&
	but cummit --no-verify -m "plus" &&
	cummit_msg_is "plus"

'

test_expect_success "hook doesn't edit cummit message (editor)" '

	echo "more plus" >> file &&
	but add file &&
	echo "more plus" > FAKE_MSG &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit --no-verify &&
	cummit_msg_is "more plus"
'

test_expect_success 'hook called in but-merge picks up cummit message' '
	test_when_finished "but branch -D newbranch" &&
	test_when_finished "but checkout -f main" &&
	but checkout --orphan newbranch &&
	but rm -f file &&
	: >file2 &&
	but add file2 &&
	but cummit --no-verify file2 -m in-side-branch &&
	but merge --allow-unrelated-histories main &&
	cummit_msg_is "new message"
'

test_expect_failure 'merge --continue remembers --no-verify' '
	test_when_finished "but branch -D newbranch" &&
	test_when_finished "but checkout -f main" &&
	but checkout main &&
	echo a >file2 &&
	but add file2 &&
	but cummit --no-verify -m "add file2 to main" &&
	but checkout -b newbranch main^ &&
	echo b >file2 &&
	but add file2 &&
	but cummit --no-verify file2 -m in-side-branch &&
	but merge --no-verify -m not-rewritten-by-hook main &&
	# resolve conflict:
	echo c >file2 &&
	but add file2 &&
	but merge --continue &&
	cummit_msg_is not-rewritten-by-hook
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

	GIT_SEQUENCE_EDITOR="\"$REWORD_EDITOR\"" but rebase -i HEAD^ &&
	cummit_msg_is "new message"

'


test_done
