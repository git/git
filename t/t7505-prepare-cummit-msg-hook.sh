#!/bin/sh

test_description='prepare-cummit-msg hook'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'set up cummits for rebasing' '
	test_cummit root &&
	test_cummit a a a &&
	test_cummit b b b &&
	but checkout -b rebase-me root &&
	test_cummit rebase-a a aa &&
	test_cummit rebase-b b bb &&
	for i in $(test_seq 1 13)
	do
		test_cummit rebase-$i c $i || return 1
	done &&
	but checkout main &&

	cat >rebase-todo <<-EOF
	pick $(but rev-parse rebase-a)
	pick $(but rev-parse rebase-b)
	fixup $(but rev-parse rebase-1)
	fixup $(but rev-parse rebase-2)
	pick $(but rev-parse rebase-3)
	fixup $(but rev-parse rebase-4)
	squash $(but rev-parse rebase-5)
	reword $(but rev-parse rebase-6)
	squash $(but rev-parse rebase-7)
	fixup $(but rev-parse rebase-8)
	fixup $(but rev-parse rebase-9)
	edit $(but rev-parse rebase-10)
	squash $(but rev-parse rebase-11)
	squash $(but rev-parse rebase-12)
	edit $(but rev-parse rebase-13)
	EOF
'

test_expect_success 'with no hook' '

	echo "foo" > file &&
	but add file &&
	but cummit -m "first"

'

test_expect_success 'setup fake editor for interactive editing' '
	write_script fake-editor <<-\EOF &&
	exit 0
	EOF

	## Not using test_set_editor here so we can easily ensure the editor variable
	## is only set for the editor tests
	FAKE_EDITOR="$(pwd)/fake-editor" &&
	export FAKE_EDITOR
'

test_expect_success 'setup prepare-cummit-msg hook' '
	test_hook --setup prepare-cummit-msg <<\EOF
GIT_DIR=$(but rev-parse --but-dir)
if test -d "$GIT_DIR/rebase-merge"
then
	rebasing=1
else
	rebasing=0
fi

get_last_cmd () {
	tail -n1 "$GIT_DIR/rebase-merge/done" | {
		read cmd id _
		but log --pretty="[$cmd %s]" -n1 $id
	}
}

if test "$2" = cummit
then
	if test $rebasing = 1
	then
		source="$3"
	else
		source=$(but rev-parse "$3")
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
'

echo dummy template > "$(but rev-parse --but-dir)/template"

test_expect_success 'with hook (-m)' '

	echo "more" >> file &&
	but add file &&
	but cummit -m "more" &&
	test "$(but log -1 --pretty=format:%s)" = "message (no editor)"

'

test_expect_success 'with hook (-m editor)' '

	echo "more" >> file &&
	but add file &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit -e -m "more more" &&
	test "$(but log -1 --pretty=format:%s)" = message

'

test_expect_success 'with hook (-t)' '

	echo "more" >> file &&
	but add file &&
	but cummit -t "$(but rev-parse --but-dir)/template" &&
	test "$(but log -1 --pretty=format:%s)" = template

'

test_expect_success 'with hook (-F)' '

	echo "more" >> file &&
	but add file &&
	(echo more | but cummit -F -) &&
	test "$(but log -1 --pretty=format:%s)" = "message (no editor)"

'

test_expect_success 'with hook (-F editor)' '

	echo "more" >> file &&
	but add file &&
	(echo more more | GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit -e -F -) &&
	test "$(but log -1 --pretty=format:%s)" = message

'

test_expect_success 'with hook (-C)' '

	head=$(but rev-parse HEAD) &&
	echo "more" >> file &&
	but add file &&
	but cummit -C $head &&
	test "$(but log -1 --pretty=format:%s)" = "$head (no editor)"

'

test_expect_success 'with hook (editor)' '

	echo "more more" >> file &&
	but add file &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit &&
	test "$(but log -1 --pretty=format:%s)" = default

'

test_expect_success 'with hook (--amend)' '

	head=$(but rev-parse HEAD) &&
	echo "more" >> file &&
	but add file &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit --amend &&
	test "$(but log -1 --pretty=format:%s)" = "$head"

'

test_expect_success 'with hook (-c)' '

	head=$(but rev-parse HEAD) &&
	echo "more" >> file &&
	but add file &&
	GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit -c $head &&
	test "$(but log -1 --pretty=format:%s)" = "$head"

'

test_expect_success 'with hook (merge)' '

	test_when_finished "but checkout -f main" &&
	but checkout -B other HEAD@{1} &&
	echo "more" >>file &&
	but add file &&
	but cummit -m other &&
	but checkout - &&
	but merge --no-ff other &&
	test "$(but log -1 --pretty=format:%s)" = "merge (no editor)"
'

test_expect_success 'with hook and editor (merge)' '

	test_when_finished "but checkout -f main" &&
	but checkout -B other HEAD@{1} &&
	echo "more" >>file &&
	but add file &&
	but cummit -m other &&
	but checkout - &&
	env GIT_EDITOR="\"\$FAKE_EDITOR\"" but merge --no-ff -e other &&
	test "$(but log -1 --pretty=format:%s)" = "merge"
'

test_rebase () {
	expect=$1 &&
	mode=$2 &&
	test_expect_$expect "with hook (rebase ${mode:--i})" '
		test_when_finished "\
			but rebase --abort
			but checkout -f main
			but branch -D tmp" &&
		but checkout -b tmp rebase-me &&
		GIT_SEQUENCE_EDITOR="cp rebase-todo" &&
		GIT_EDITOR="\"$FAKE_EDITOR\"" &&
		(
			export GIT_SEQUENCE_EDITOR GIT_EDITOR &&
			test_must_fail but rebase -i $mode b &&
			echo x >a &&
			but add a &&
			test_must_fail but rebase --continue &&
			echo x >b &&
			but add b &&
			but cummit &&
			but rebase --continue &&
			echo y >a &&
			but add a &&
			but cummit &&
			but rebase --continue &&
			echo y >b &&
			but add b &&
			but rebase --continue
		) &&
		but log --pretty=%s -g -n18 HEAD@{1} >actual &&
		test_cmp "$TEST_DIRECTORY/t7505/expected-rebase${mode:--i}" actual
	'
}

test_rebase success

test_expect_success 'with hook (cherry-pick)' '
	test_when_finished "but checkout -f main" &&
	but checkout -B other b &&
	but cherry-pick rebase-1 &&
	test "$(but log -1 --pretty=format:%s)" = "message (no editor)"
'

test_expect_success 'with hook and editor (cherry-pick)' '
	test_when_finished "but checkout -f main" &&
	but checkout -B other b &&
	but cherry-pick -e rebase-1 &&
	test "$(but log -1 --pretty=format:%s)" = merge
'

test_expect_success 'setup: cummit-msg hook that always fails' '
	test_hook --setup --clobber prepare-cummit-msg <<-\EOF
	exit 1
	EOF
'

test_expect_success 'with failing hook' '

	test_when_finished "but checkout -f main" &&
	head=$(but rev-parse HEAD) &&
	echo "more" >> file &&
	but add file &&
	test_must_fail env GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit -c $head

'

test_expect_success 'with failing hook (--no-verify)' '

	test_when_finished "but checkout -f main" &&
	head=$(but rev-parse HEAD) &&
	echo "more" >> file &&
	but add file &&
	test_must_fail env GIT_EDITOR="\"\$FAKE_EDITOR\"" but cummit --no-verify -c $head

'

test_expect_success 'with failing hook (merge)' '

	test_when_finished "but checkout -f main" &&
	but checkout -B other HEAD@{1} &&
	echo "more" >> file &&
	but add file &&
	test_hook --remove prepare-cummit-msg &&
	but cummit -m other &&
	test_hook --setup prepare-cummit-msg <<-\EOF &&
	exit 1
	EOF
	but checkout - &&
	test_must_fail but merge --no-ff other

'

test_expect_success 'with failing hook (cherry-pick)' '
	test_when_finished "but checkout -f main" &&
	but checkout -B other b &&
	test_must_fail but cherry-pick rebase-1 2>actual &&
	test $(grep -c prepare-cummit-msg actual) = 1
'

test_done
