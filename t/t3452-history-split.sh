#!/bin/sh

test_description='tests for git-history split subcommand'

. ./test-lib.sh

# The fake editor takes multiple arguments, each of which represents a commit
# message. Subsequent invocations of the editor will then yield those messages
# in order.
#
set_fake_editor () {
	printf "%s\n" "$@" >fake-input &&
	write_script fake-editor.sh <<-\EOF &&
	head -n1 fake-input >"$1"
	sed 1d fake-input >fake-input.trimmed &&
	mv fake-input.trimmed fake-input
	EOF
	test_set_editor "$(pwd)"/fake-editor.sh
}

expect_log () {
	git log --format="%s" >actual &&
	cat >expect &&
	test_cmp expect actual
}

expect_tree_entries () {
	git ls-tree --name-only "$1" >actual &&
	cat >expect &&
	test_cmp expect actual
}

test_expect_success 'refuses to work with merge commits' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		git branch branch &&
		test_commit ours &&
		git switch branch &&
		test_commit theirs &&
		git switch - &&
		git merge theirs &&
		test_must_fail git history split HEAD 2>err &&
		test_grep "cannot rearrange commit history with merges" err &&
		test_must_fail git history split HEAD~ 2>err &&
		test_grep "cannot rearrange commit history with merges" err
	)
'

test_expect_success 'refuses to work with unrelated commits' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		git branch branch &&
		test_commit ours &&
		git switch branch &&
		test_commit theirs &&
		test_must_fail git history split ours 2>err &&
		test_grep "commit must be reachable from current HEAD commit" err
	)
'

test_expect_success 'can split up tip commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&

		git symbolic-ref HEAD >expect &&
		set_fake_editor "first" "second" &&
		git history split HEAD <<-EOF &&
		y
		n
		EOF
		git symbolic-ref HEAD >actual &&
		test_cmp expect actual &&

		expect_log <<-EOF &&
		second
		first
		initial
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		bar
		initial.t
		EOF

		expect_tree_entries HEAD <<-EOF &&
		bar
		foo
		initial.t
		EOF

		git reflog >reflog &&
		test_grep "split: updating HEAD" reflog
	)
'

test_expect_success 'can split up root commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch bar foo &&
		git add . &&
		git commit -m root &&
		test_commit tip &&

		set_fake_editor "first" "second" &&
		git history split HEAD~ <<-EOF &&
		y
		n
		EOF

		expect_log <<-EOF &&
		tip
		second
		first
		EOF

		expect_tree_entries HEAD~2 <<-EOF &&
		bar
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		bar
		foo
		EOF

		expect_tree_entries HEAD <<-EOF
		bar
		foo
		tip.t
		EOF
	)
'

test_expect_success 'can split up in-between commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&
		test_commit tip &&

		set_fake_editor "first" "second" &&
		git history split HEAD~ <<-EOF &&
		y
		n
		EOF

		expect_log <<-EOF &&
		tip
		second
		first
		initial
		EOF

		expect_tree_entries HEAD~2 <<-EOF &&
		bar
		initial.t
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		bar
		foo
		initial.t
		EOF

		expect_tree_entries HEAD <<-EOF
		bar
		foo
		initial.t
		tip.t
		EOF
	)
'

test_expect_success 'can pick multiple hunks' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch bar baz foo qux &&
		git add . &&
		git commit -m split-me &&

		set_fake_editor "first" "second" &&
		git history split HEAD <<-EOF &&
		y
		n
		y
		n
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		bar
		foo
		EOF

		expect_tree_entries HEAD <<-EOF
		bar
		baz
		foo
		qux
		EOF
	)
'


test_expect_success 'can use only last hunk' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&

		set_fake_editor "first" "second" &&
		git history split HEAD <<-EOF &&
		n
		y
		EOF

		expect_log <<-EOF &&
		second
		first
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		foo
		EOF

		expect_tree_entries HEAD <<-EOF
		bar
		foo
		EOF
	)
'

test_expect_success 'aborts with empty commit message' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&

		set_fake_editor "" &&
		test_must_fail git history split HEAD <<-EOF 2>err &&
		y
		n
		EOF
		test_grep "Aborting commit due to empty commit message." err
	)
'

test_expect_success 'commit message editor sees split-out changes' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&

		write_script fake-editor.sh <<-\EOF &&
		cat "$1" >>MESSAGES &&
		echo "some commit message" >"$1"
		EOF
		test_set_editor "$(pwd)"/fake-editor.sh &&

		git history split HEAD <<-EOF &&
		y
		n
		EOF

		# Note that we expect to see the messages twice, once for each
		# of the commits. The committed files are different though.
		cat >expect <<-EOF &&
		split-me

		# Please enter the commit message for the split-out changes. Lines starting
		# with ${SQ}#${SQ} will be ignored, and an empty message aborts the commit.
		# Changes to be committed:
		#	new file:   bar
		#
		split-me

		# Please enter the commit message for the split-out changes. Lines starting
		# with ${SQ}#${SQ} will be ignored, and an empty message aborts the commit.
		# Changes to be committed:
		#	new file:   foo
		#
		EOF
		test_cmp expect MESSAGES &&

		expect_log <<-EOF
		some commit message
		some commit message
		EOF
	)
'

test_expect_success 'can use pathspec to limit what gets split' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&

		set_fake_editor "first" "second" &&
		git history split HEAD -- foo <<-EOF &&
		y
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		foo
		EOF

		expect_tree_entries HEAD <<-EOF
		bar
		foo
		EOF
	)
'

test_expect_success 'refuses to create empty split-out commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&

		test_must_fail git history split HEAD 2>err <<-EOF &&
		n
		n
		EOF
		test_grep "split commit is empty" err
	)
'

test_expect_success 'hooks are not executed for rewritten commits' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&
		old_head=$(git rev-parse HEAD) &&

		ORIG_PATH="$(pwd)" &&
		export ORIG_PATH &&
		for hook in prepare-commit-msg pre-commit post-commit post-rewrite commit-msg
		do
			write_script .git/hooks/$hook <<-\EOF || exit 1
			touch "$ORIG_PATH/hooks.log
			EOF
		done &&

		set_fake_editor "first" "second" &&
		git history split HEAD <<-EOF &&
		y
		n
		EOF

		expect_log <<-EOF &&
		second
		first
		EOF

		test_path_is_missing hooks.log
	)
'

test_expect_success 'refuses to create empty original commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&

		test_must_fail git history split HEAD 2>err <<-EOF &&
		y
		y
		EOF
		test_grep "split commit tree matches original commit" err
	)
'

test_expect_success 'retains changes in the worktree and index' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		echo a >a &&
		echo b >b &&
		git add . &&
		git commit -m "initial commit" &&
		echo a-modified >a &&
		echo b-modified >b &&
		git add b &&
		set_fake_editor "a-only" "remainder" &&
		git history split HEAD <<-EOF &&
		y
		n
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		a
		EOF
		expect_tree_entries HEAD <<-EOF &&
		a
		b
		EOF

		cat >expect <<-\EOF &&
		 M a
		M  b
		?? actual
		?? expect
		?? fake-editor.sh
		?? fake-input
		EOF
		git status --porcelain >actual &&
		test_cmp expect actual
	)
'

test_done
