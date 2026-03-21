#!/bin/sh

test_description='tests for git-history split subcommand'

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-log-graph.sh"

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

expect_graph () {
	cat >expect &&
	lib_test_cmp_graph --graph --format=%s "$@"
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
		test_grep "cannot split up merge commit" err &&
		test_must_fail git history split HEAD~ 2>err &&
		test_grep "replaying merge commits is not supported yet" err
	)
'

test_expect_success 'errors on missing commit argument' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_must_fail git history split 2>err &&
		test_grep "command expects a committish" err
	)
'

test_expect_success 'errors on unknown revision' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_must_fail git history split does-not-exist 2>err &&
		test_grep "commit cannot be found" err
	)
'

test_expect_success '--dry-run does not modify any refs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&

		git refs list --include-root-refs >before &&

		set_fake_editor "first" "second" &&
		git history split --dry-run HEAD <<-EOF &&
		y
		n
		EOF

		git refs list --include-root-refs >after &&
		test_cmp before after
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

test_expect_success 'can split HEAD only' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		touch a b &&
		git add . &&
		git commit -m split-me &&
		git branch unrelated &&

		set_fake_editor "ours-a" "ours-b" &&
		git history split --update-refs=head HEAD <<-EOF &&
		y
		n
		EOF
		expect_graph --branches <<-EOF
		* ours-b
		* ours-a
		| * split-me
		|/
		* base
		EOF
	)
'

test_expect_success 'can split detached HEAD' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&
		git checkout --detach HEAD &&

		set_fake_editor "first" "second" &&
		git history split --update-refs=head HEAD <<-EOF &&
		y
		n
		EOF

		# HEAD should be detached and updated.
		test_must_fail git symbolic-ref HEAD &&

		expect_log <<-EOF
		second
		first
		initial
		EOF
	)
'

test_expect_success 'can split commit in unrelated branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		git branch ours &&
		git switch --create theirs &&
		touch theirs-a theirs-b &&
		git add . &&
		git commit -m theirs &&
		git switch ours &&
		test_commit ours &&

		# With --update-refs=head it is not possible to split up a
		# commit that is unrelated to HEAD.
		test_must_fail git history split --update-refs=head theirs 2>err &&
		test_grep "rewritten commit must be an ancestor of HEAD" err &&

		set_fake_editor "theirs-rewritten-a" "theirs-rewritten-b" &&
		git history split theirs <<-EOF &&
		y
		n
		EOF
		expect_graph --branches <<-EOF &&
		* ours
		| * theirs-rewritten-b
		| * theirs-rewritten-a
		|/
		* base
		EOF

		expect_tree_entries theirs~ <<-EOF &&
		base.t
		theirs-a
		EOF

		expect_tree_entries theirs <<-EOF
		base.t
		theirs-a
		theirs-b
		EOF
	)
'

test_expect_success 'updates multiple descendant branches' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit base &&
		touch file-a file-b &&
		git add . &&
		git commit -m split-me &&
		git branch branch &&
		test_commit on-main &&
		git switch branch &&
		test_commit on-branch &&
		git switch main &&

		set_fake_editor "split-a" "split-b" &&
		git history split HEAD~ <<-EOF &&
		y
		n
		EOF

		# Both branches should now descend from the split commits.
		expect_graph --branches <<-EOF
		* on-branch
		| * on-main
		|/
		* split-b
		* split-a
		* base
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

test_expect_success 'can split commit with file deletions' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		echo a >a &&
		echo b >b &&
		echo c >c &&
		git add . &&
		git commit -m base &&
		git rm a b &&
		git commit -m delete-both &&

		set_fake_editor "delete-a" "delete-b" &&
		git history split HEAD <<-EOF &&
		y
		n
		EOF

		expect_log <<-EOF &&
		delete-b
		delete-a
		base
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		b
		c
		EOF

		expect_tree_entries HEAD <<-EOF
		c
		EOF
	)
'

test_expect_success 'preserves original authorship' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		touch bar foo &&
		git add . &&
		GIT_AUTHOR_NAME="Other Author" \
		GIT_AUTHOR_EMAIL="other@example.com" \
		git commit -m split-me &&

		set_fake_editor "first" "second" &&
		git history split HEAD <<-EOF &&
		y
		n
		EOF

		git log -1 --format="%an <%ae>" HEAD~ >actual &&
		echo "Other Author <other@example.com>" >expect &&
		test_cmp expect actual &&

		git log -1 --format="%an <%ae>" HEAD >actual &&
		test_cmp expect actual
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

test_expect_success 'pathspec matching no files produces empty split error' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		touch bar foo &&
		git add . &&
		git commit -m split-me &&

		set_fake_editor "first" "second" &&
		test_must_fail git history split HEAD -- nonexistent 2>err &&
		test_grep "split commit is empty" err
	)
'

test_expect_success 'split with multiple pathspecs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		touch a b c d &&
		git add . &&
		git commit -m split-me &&

		# Only a and c should be offered for splitting.
		set_fake_editor "split-ac" "remainder" &&
		git history split HEAD -- a c <<-EOF &&
		y
		y
		EOF

		expect_tree_entries HEAD~ <<-EOF &&
		a
		c
		initial.t
		EOF

		expect_tree_entries HEAD <<-EOF
		a
		b
		c
		d
		initial.t
		EOF
	)
'

test_expect_success 'split with file mode change' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		echo content >script &&
		git add . &&
		git commit -m base &&
		test_chmod +x script &&
		echo change >script &&
		git commit -a -m "mode and content change" &&

		set_fake_editor "mode-change" "content-change" &&
		git history split HEAD <<-EOF &&
		y
		n
		EOF

		expect_log <<-EOF
		content-change
		mode-change
		base
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
			touch "$ORIG_PATH"/hooks.log
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
