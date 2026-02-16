#!/bin/sh

test_description='tests for git-history reword subcommand'

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-log-graph.sh"

reword_with_message () {
	cat >message &&
	write_script fake-editor.sh <<-\EOF &&
	cp message "$1"
	EOF
	test_set_editor "$(pwd)"/fake-editor.sh &&
	git history reword "$@" &&
	rm fake-editor.sh message
}

expect_graph () {
	cat >expect &&
	lib_test_cmp_graph --graph --format=%s "$@"
}

expect_log () {
	git log --format="%s" "$@" >actual &&
	cat >expect &&
	test_cmp expect actual
}

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

		expect_log <<-\EOF &&
		third reworded
		second
		first
		EOF

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

		expect_log <<-\EOF
		third
		second reworded
		first
		EOF
	)
'

test_expect_success 'can reword commit in the middle even on detached head' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		test_commit third_on_main &&
		git checkout --detach HEAD^ &&
		test_commit third_on_head &&

		reword_with_message HEAD~ <<-EOF &&
		second reworded
		EOF

		expect_graph HEAD --branches <<-\EOF
		* third_on_head
		| * third_on_main
		|/
		* second reworded
		* first
		EOF
       )
'

test_expect_success 'can reword the detached head' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		git checkout --detach HEAD &&
		test_commit third &&

		reword_with_message HEAD <<-EOF &&
		third reworded
		EOF

		expect_log <<-\EOF
		third reworded
		second
		first
		EOF
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

		expect_log <<-\EOF
		third
		second
		first reworded
		EOF
	)
'

test_expect_success 'can reword in a bare repo' '
	test_when_finished "rm -rf repo repo.git" &&
	git init repo &&
	test_commit -C repo first &&
	git clone --bare repo repo.git &&
	(
		cd repo.git &&
		reword_with_message HEAD <<-EOF &&
		reworded
		EOF

		expect_log <<-\EOF
		reworded
		EOF
	)
'

test_expect_success 'can reword a commit on a different branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		git branch theirs &&
		test_commit ours &&
		git switch theirs &&
		test_commit theirs &&

		git rev-parse ours >ours-before &&
		reword_with_message theirs <<-EOF &&
		Reworded theirs
		EOF
		git rev-parse ours >ours-after &&
		test_cmp ours-before ours-after &&

		expect_graph --branches <<-\EOF
		* Reworded theirs
		| * ours
		|/
		* base
		EOF
	)
'

test_expect_success 'can reword a merge commit' '
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

		# It is not possible to replay merge commits embedded in the
		# history (yet).
		test_must_fail git -c core.editor=false history reword HEAD~ 2>err &&
		test_grep "replaying merge commits is not supported yet" err &&

		# But it is possible to reword a merge commit directly.
		reword_with_message HEAD <<-EOF &&
		Reworded merge commit
		EOF
		expect_graph <<-\EOF
		*   Reworded merge commit
		|\
		| * theirs
		* | ours
		|/
		* base
		EOF
	)
'

test_expect_success '--ref-action=print prints ref updates without modifying repo' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit base &&
		git branch branch &&
		test_commit ours &&
		git switch branch &&
		test_commit theirs &&

		git refs list >refs-expect &&
		reword_with_message --ref-action=print base >updates <<-\EOF &&
		reworded commit
		EOF
		git refs list >refs-actual &&
		test_cmp refs-expect refs-actual &&

		test_grep "update refs/heads/branch" updates &&
		test_grep "update refs/heads/main" updates &&
		git update-ref --stdin <updates &&
		expect_log --branches <<-\EOF
		theirs
		ours
		reworded commit
		EOF
	)
'

test_expect_success '--ref-action=head updates only HEAD' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit base &&
		git branch branch &&
		test_commit theirs &&
		git switch branch &&
		test_commit ours &&

		# When told to update HEAD, only, the command will refuse to
		# rewrite commits that are not an ancestor of HEAD.
		test_must_fail git -c core.editor=false history reword --ref-action=head theirs 2>err &&
		test_grep "rewritten commit must be an ancestor of HEAD" err &&

		reword_with_message --ref-action=head base >updates <<-\EOF &&
		reworded base
		EOF
		expect_log HEAD <<-\EOF &&
		ours
		reworded base
		EOF
		expect_log main <<-\EOF
		theirs
		base
		EOF
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
