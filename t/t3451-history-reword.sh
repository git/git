#!/bin/sh

test_description='tests for git-history reword subcommand'

. ./test-lib.sh

reword_with_message () {
	cat >message &&
	write_script fake-editor.sh <<-\EOF &&
	cp message "$1"
	EOF
	test_set_editor "$(pwd)"/fake-editor.sh &&
	git history reword "$@" &&
	rm fake-editor.sh message
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
		test_must_fail git history reword HEAD~ 2>err &&
		test_grep "cannot rearrange commit history with merges" err &&
		test_must_fail git history reword HEAD 2>err &&
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
		test_must_fail git history reword ours 2>err &&
		test_grep "commit must be reachable from current HEAD commit" err
	)
'

test_expect_success 'can reword tip of a branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		test_commit third &&

		git symbolic-ref HEAD >expect &&
		reword_with_message HEAD <<-EOF &&
		third reworded
		EOF
		git symbolic-ref HEAD >actual &&
		test_cmp expect actual &&

		cat >expect <<-EOF &&
		third reworded
		second
		first
		EOF
		git log --format=%s >actual &&
		test_cmp expect actual &&

		git reflog >reflog &&
		test_grep "reword: updating HEAD" reflog
	)
'

test_expect_success 'can reword commit in the middle' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		test_commit third &&

		git symbolic-ref HEAD >expect &&
		reword_with_message HEAD~ <<-EOF &&
		second reworded
		EOF
		git symbolic-ref HEAD >actual &&
		test_cmp expect actual &&

		cat >expect <<-EOF &&
		third
		second reworded
		first
		EOF
		git log --format=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'can reword root commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		test_commit third &&
		reword_with_message HEAD~2 <<-EOF &&
		first reworded
		EOF

		cat >expect <<-EOF &&
		third
		second
		first reworded
		EOF
		git log --format=%s >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'editor shows proper status' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&

		write_script fake-editor.sh <<-\EOF &&
		cp "$1" . &&
		printf "\namend a comment\n" >>"$1"
		EOF
		test_set_editor "$(pwd)"/fake-editor.sh &&
		git history reword HEAD &&

		cat >expect <<-EOF &&
		first

		# Please enter the commit message for the reworded changes. Lines starting
		# with ${SQ}#${SQ} will be ignored, and an empty message aborts the commit.
		# Changes to be committed:
		#	new file:   first.t
		#
		EOF
		test_cmp expect COMMIT_EDITMSG &&

		test_commit_message HEAD <<-\EOF
		first

		amend a comment
		EOF
	)
'

# For now, git-history(1) does not yet execute any hooks. This is subject to
# change in the future, and if it does this test here is expected to start
# failing. In other words, this test is not an endorsement of the current
# status quo.
test_expect_success 'hooks are not executed for rewritten commits' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		test_commit third &&

		ORIG_PATH="$(pwd)" &&
		export ORIG_PATH &&
		for hook in prepare-commit-msg pre-commit post-commit post-rewrite commit-msg
		do
			write_script .git/hooks/$hook <<-\EOF || exit 1
			touch "$ORIG_PATH/hooks.log
			EOF
		done &&

		reword_with_message HEAD~ <<-EOF &&
		second reworded
		EOF

		cat >expect <<-EOF &&
		third
		second reworded
		first
		EOF
		git log --format=%s >actual &&
		test_cmp expect actual &&

		test_path_is_missing hooks.log
	)
'

test_expect_success 'aborts with empty commit message' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&

		! reword_with_message HEAD 2>err </dev/null &&
		test_grep "Aborting commit due to empty commit message." err
	)
'

test_expect_success 'retains changes in the worktree and index' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch a b &&
		git add . &&
		git commit -m "initial commit" &&
		echo foo >a &&
		echo bar >b &&
		git add b &&
		reword_with_message HEAD <<-EOF &&
		message
		EOF
		cat >expect <<-\EOF &&
		 M a
		M  b
		?? actual
		?? expect
		EOF
		git status --porcelain >actual &&
		test_cmp expect actual
	)
'

test_done
