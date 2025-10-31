#!/bin/sh

test_description='tests for git-history split subcommand'

. ./test-lib.sh

set_fake_editor () {
	write_script fake-editor.sh <<-EOF &&
	echo "$@" >"\$1"
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
		test_grep "split commit must be reachable from current HEAD commit" err
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
		set_fake_editor "split-out commit" &&
		git history split HEAD <<-EOF &&
		y
		n
		EOF
		git symbolic-ref HEAD >actual &&
		test_cmp expect actual &&

		expect_log <<-EOF &&
		split-me
		split-out commit
		initial
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		bar
		initial.t
		EOF

		expect_tree_entries HEAD <<-EOF
		bar
		foo
		initial.t
		EOF
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

		set_fake_editor "split-out commit" &&
		git history split HEAD~ <<-EOF &&
		y
		n
		EOF

		expect_log <<-EOF &&
		tip
		root
		split-out commit
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

		set_fake_editor "split-out commit" &&
		git history split HEAD~ <<-EOF &&
		y
		n
		EOF

		expect_log <<-EOF &&
		tip
		split-me
		split-out commit
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

		set_fake_editor "split-out-commit" &&
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

		set_fake_editor "split-out commit" &&
		git history split HEAD <<-EOF &&
		n
		y
		EOF

		expect_log <<-EOF &&
		split-me
		split-out commit
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
		cp "$1" . &&
		echo "some commit message" >>"$1"
		EOF
		test_set_editor "$(pwd)"/fake-editor.sh &&

		git history split HEAD <<-EOF &&
		y
		n
		EOF

		cat >expect <<-EOF &&

		# Please enter the commit message for the split-out changes. Lines starting
		# with ${SQ}#${SQ} will be ignored.
		# Changes to be committed:
		#	new file:   bar
		#
		EOF
		test_cmp expect COMMIT_EDITMSG &&

		expect_log <<-EOF
		split-me
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

		set_fake_editor "split-out commit" &&
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

test_expect_success 'hooks are executed for rewritten commits' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&
		old_head=$(git rev-parse HEAD) &&

		write_script .git/hooks/prepare-commit-msg <<-EOF &&
		touch "$(pwd)/hooks.log"
		EOF
		write_script .git/hooks/post-commit <<-EOF &&
		touch "$(pwd)/hooks.log"
		EOF
		write_script .git/hooks/post-rewrite <<-EOF &&
		touch "$(pwd)/hooks.log"
		EOF

		set_fake_editor "split-out commit" &&
		git history split HEAD <<-EOF &&
		y
		n
		EOF

		expect_log <<-EOF &&
		split-me
		split-out commit
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
		set_fake_editor "a-only" &&
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
		EOF
		git status --porcelain >actual &&
		test_cmp expect actual
	)
'

test_done
