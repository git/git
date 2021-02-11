#!/bin/sh

test_description='prepare-commit-msg hook'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'set up commits for rebasing' '
	test_commit root &&
	test_commit a a a &&
	test_commit b b b &&
	git checkout -b rebase-me root &&
	test_commit rebase-a a aa &&
	test_commit rebase-b b bb &&
	for i in $(test_seq 1 13)
	do
		test_commit rebase-$i c $i
	done &&
	git checkout main &&

	cat >rebase-todo <<-EOF
	pick $(git rev-parse rebase-a)
	pick $(git rev-parse rebase-b)
	fixup $(git rev-parse rebase-1)
	fixup $(git rev-parse rebase-2)
	pick $(git rev-parse rebase-3)
	fixup $(git rev-parse rebase-4)
	squash $(git rev-parse rebase-5)
	reword $(git rev-parse rebase-6)
	squash $(git rev-parse rebase-7)
	fixup $(git rev-parse rebase-8)
	fixup $(git rev-parse rebase-9)
	edit $(git rev-parse rebase-10)
	squash $(git rev-parse rebase-11)
	squash $(git rev-parse rebase-12)
	edit $(git rev-parse rebase-13)
	EOF
'

test_expect_success 'with no hook' '

	echo "foo" > file &&
	git add file &&
	git commit -m "first"

'

# set up fake editor for interactive editing
cat > fake-editor <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x fake-editor

## Not using test_set_editor here so we can easily ensure the editor variable
## is only set for the editor tests
FAKE_EDITOR="$(pwd)/fake-editor"
export FAKE_EDITOR

# now install hook that always succeeds and adds a message
HOOKDIR="$(git rev-parse --git-dir)/hooks"
HOOK="$HOOKDIR/prepare-commit-msg"
mkdir -p "$HOOKDIR"
echo "#!$SHELL_PATH" > "$HOOK"
cat >> "$HOOK" <<'EOF'

GIT_DIR=$(git rev-parse --git-dir)
if test -d "$GIT_DIR/rebase-merge"
then
	rebasing=1
else
	rebasing=0
fi

get_last_cmd () {
	tail -n1 "$GIT_DIR/rebase-merge/done" | {
		read cmd id _
		git log --pretty="[$cmd %s]" -n1 $id
	}
}

if test "$2" = commit
then
	if test $rebasing = 1
	then
		source="$3"
	else
		source=$(git rev-parse "$3")
	fi
else
	source=${2-default}
fi
test "$GIT_EDITOR" = : && source="$source (no editor)"

if test $rebasing = 1
then
	echo "$source $(get_last_cmd)" >"$1"
else
	sed -e "1s/.*/$source/" "$1" >msg.tmp
	mv msg.tmp "$1"
fi
exit 0
EOF
chmod +x "$HOOK"

echo dummy template > "$(git rev-parse --git-dir)/template"

test_expect_success 'with hook (-m)' '

	echo "more" >> file &&
	git add file &&
	git commit -m "more" &&
	test "$(git log -1 --pretty=format:%s)" = "message (no editor)"

'

test_expect_success 'with hook (-m editor)' '

	echo "more" >> file &&
	git add file &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit -e -m "more more" &&
	test "$(git log -1 --pretty=format:%s)" = message

'

test_expect_success 'with hook (-t)' '

	echo "more" >> file &&
	git add file &&
	git commit -t "$(git rev-parse --git-dir)/template" &&
	test "$(git log -1 --pretty=format:%s)" = template

'

test_expect_success 'with hook (-F)' '

	echo "more" >> file &&
	git add file &&
	(echo more | git commit -F -) &&
	test "$(git log -1 --pretty=format:%s)" = "message (no editor)"

'

test_expect_success 'with hook (-F editor)' '

	echo "more" >> file &&
	git add file &&
	(echo more more | GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit -e -F -) &&
	test "$(git log -1 --pretty=format:%s)" = message

'

test_expect_success 'with hook (-C)' '

	head=$(git rev-parse HEAD) &&
	echo "more" >> file &&
	git add file &&
	git commit -C $head &&
	test "$(git log -1 --pretty=format:%s)" = "$head (no editor)"

'

test_expect_success 'with hook (editor)' '

	echo "more more" >> file &&
	git add file &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit &&
	test "$(git log -1 --pretty=format:%s)" = default

'

test_expect_success 'with hook (--amend)' '

	head=$(git rev-parse HEAD) &&
	echo "more" >> file &&
	git add file &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit --amend &&
	test "$(git log -1 --pretty=format:%s)" = "$head"

'

test_expect_success 'with hook (-c)' '

	head=$(git rev-parse HEAD) &&
	echo "more" >> file &&
	git add file &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit -c $head &&
	test "$(git log -1 --pretty=format:%s)" = "$head"

'

test_expect_success 'with hook (merge)' '

	test_when_finished "git checkout -f main" &&
	git checkout -B other HEAD@{1} &&
	echo "more" >>file &&
	git add file &&
	git commit -m other &&
	git checkout - &&
	git merge --no-ff other &&
	test "$(git log -1 --pretty=format:%s)" = "merge (no editor)"
'

test_expect_success 'with hook and editor (merge)' '

	test_when_finished "git checkout -f main" &&
	git checkout -B other HEAD@{1} &&
	echo "more" >>file &&
	git add file &&
	git commit -m other &&
	git checkout - &&
	env GIT_EDITOR="\"\$FAKE_EDITOR\"" git merge --no-ff -e other &&
	test "$(git log -1 --pretty=format:%s)" = "merge"
'

test_rebase () {
	expect=$1 &&
	mode=$2 &&
	test_expect_$expect "with hook (rebase ${mode:--i})" '
		test_when_finished "\
			git rebase --abort
			git checkout -f main
			git branch -D tmp" &&
		git checkout -b tmp rebase-me &&
		GIT_SEQUENCE_EDITOR="cp rebase-todo" &&
		GIT_EDITOR="\"$FAKE_EDITOR\"" &&
		(
			export GIT_SEQUENCE_EDITOR GIT_EDITOR &&
			test_must_fail git rebase -i $mode b &&
			echo x >a &&
			git add a &&
			test_must_fail git rebase --continue &&
			echo x >b &&
			git add b &&
			git commit &&
			git rebase --continue &&
			echo y >a &&
			git add a &&
			git commit &&
			git rebase --continue &&
			echo y >b &&
			git add b &&
			git rebase --continue
		) &&
		git log --pretty=%s -g -n18 HEAD@{1} >actual &&
		test_cmp "$TEST_DIRECTORY/t7505/expected-rebase${mode:--i}" actual
	'
}

test_rebase success
test_have_prereq !REBASE_P || test_rebase success -p

test_expect_success 'with hook (cherry-pick)' '
	test_when_finished "git checkout -f main" &&
	git checkout -B other b &&
	git cherry-pick rebase-1 &&
	test "$(git log -1 --pretty=format:%s)" = "message (no editor)"
'

test_expect_success 'with hook and editor (cherry-pick)' '
	test_when_finished "git checkout -f main" &&
	git checkout -B other b &&
	git cherry-pick -e rebase-1 &&
	test "$(git log -1 --pretty=format:%s)" = merge
'

cat > "$HOOK" <<'EOF'
#!/bin/sh
exit 1
EOF

test_expect_success 'with failing hook' '

	test_when_finished "git checkout -f main" &&
	head=$(git rev-parse HEAD) &&
	echo "more" >> file &&
	git add file &&
	test_must_fail env GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit -c $head

'

test_expect_success 'with failing hook (--no-verify)' '

	test_when_finished "git checkout -f main" &&
	head=$(git rev-parse HEAD) &&
	echo "more" >> file &&
	git add file &&
	test_must_fail env GIT_EDITOR="\"\$FAKE_EDITOR\"" git commit --no-verify -c $head

'

test_expect_success 'with failing hook (merge)' '

	test_when_finished "git checkout -f main" &&
	git checkout -B other HEAD@{1} &&
	echo "more" >> file &&
	git add file &&
	rm -f "$HOOK" &&
	git commit -m other &&
	write_script "$HOOK" <<-EOF &&
	exit 1
	EOF
	git checkout - &&
	test_must_fail git merge --no-ff other

'

test_expect_success 'with failing hook (cherry-pick)' '
	test_when_finished "git checkout -f main" &&
	git checkout -B other b &&
	test_must_fail git cherry-pick rebase-1 2>actual &&
	test $(grep -c prepare-commit-msg actual) = 1
'

test_done
