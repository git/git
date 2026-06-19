#!/bin/sh

test_description='tests for git-history drop subcommand'

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-log-graph.sh"

expect_graph () {
	cat >expect &&
	lib_test_cmp_graph --format=%s "$@"
}

expect_log () {
	git log --format="%s" "$@" >actual &&
	cat >expect &&
	test_cmp expect actual
}

test_expect_success 'errors on missing commit argument' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_must_fail git history drop 2>err &&
		test_grep "command expects a single revision" err
	)
'

test_expect_success 'errors on too many arguments' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_must_fail git history drop HEAD HEAD 2>err &&
		test_grep "command expects a single revision" err
	)
'

test_expect_success 'errors on unknown revision' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_must_fail git history drop does-not-exist 2>err &&
		test_grep "commit cannot be found: does-not-exist" err
	)
'

test_expect_success 'errors with invalid --empty= value' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_commit second &&
		test_must_fail git history drop --empty=bogus HEAD 2>err &&
		test_grep "unrecognized.*--empty.*bogus" err
	)
'

test_expect_success 'drops a commit in the middle and replays descendants' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		test_commit third &&

		git symbolic-ref HEAD >expect &&
		git history drop HEAD~ &&
		git symbolic-ref HEAD >actual &&
		test_cmp expect actual &&

		expect_log <<-\EOF &&
		third
		first
		EOF

		test_must_fail git show HEAD:second.t &&
		test_path_is_missing second.t &&

		git reflog >reflog &&
		test_grep "drop: dropping HEAD~" reflog
	)
'

test_expect_success 'drops the HEAD commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&

		git history drop HEAD &&

		expect_log <<-\EOF
		first
		EOF
	)
'

test_expect_success 'drops a commit on detached HEAD' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		test_commit third &&
		git checkout --detach HEAD &&

		git history drop HEAD~ &&

		expect_log <<-\EOF
		third
		first
		EOF
	)
'

# Note: in this case it would actually be fine to drop the root commit, as we
# do have a descendant commit, and no reference points to the root commit
# directly. So this is something that we may relax eventually.
test_expect_success 'refuses to drop the root commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&

		test_must_fail git history drop HEAD~ 2>err &&
		test_grep "cannot drop root commit" err
	)
'

# In contrast to the above case, we actually don't want to drop the root commit
# here as that would cause us to end up with an empty commit graph.
test_expect_success 'refuses to drop the root commit when branch becomes empty' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&

		test_must_fail git history drop HEAD 2>err &&
		test_grep "cannot drop root commit" err
	)
'

test_expect_success 'refuses to drop a merge commit' '
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

		test_must_fail git history drop HEAD 2>err &&
		test_grep "cannot drop merge commit" err
	)
'

test_expect_success 'refuses when descendants contain a merge commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		test_commit middle &&
		git branch branch &&
		test_commit ours &&
		git switch branch &&
		test_commit theirs &&
		git switch - &&
		git merge theirs &&

		test_must_fail git history drop middle 2>err &&
		test_grep "replaying merge commits is not supported yet" err
	)
'

test_expect_success 'works in a bare repository' '
	test_when_finished "rm -rf repo repo.git" &&

	git init repo &&
	test_commit -C repo first &&
	test_commit -C repo second &&
	test_commit -C repo third &&

	git clone --bare repo repo.git &&
	(
		cd repo.git &&

		git history drop HEAD~ &&
		expect_log <<-\EOF
		third
		first
		EOF
	)
'

test_expect_success 'updates branches on other lines of descent' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		test_commit target &&
		git branch theirs &&
		test_commit ours &&
		git switch theirs &&
		test_commit theirs &&

		expect_graph --branches <<-\EOF &&
		* theirs
		| * ours
		|/
		* target
		* base
		EOF

		git history drop target &&

		expect_graph --branches <<-\EOF
		* ours
		| * theirs
		|/
		* base
		EOF
	)
'

test_expect_success 'moves branch pointing at dropped commit to its parent' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		git branch points-at-second &&
		test_commit third &&

		git rev-parse first >expect &&
		git history drop second &&
		git rev-parse points-at-second >actual &&
		test_cmp expect actual &&

		expect_log --format="%s %D" --branches <<-\EOF
		third HEAD -> main
		first tag: first, points-at-second
		EOF
	)
'

test_expect_success '--dry-run prints ref updates without modifying repo' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit base &&
		git branch branch &&
		test_commit middle &&
		test_commit ours &&
		git switch branch &&
		test_commit theirs &&

		git refs list >refs-expect &&
		git history drop --dry-run main~ >updates &&
		git refs list >refs-actual &&
		test_cmp refs-expect refs-actual &&
		test_grep "update refs/heads/main" updates &&

		git update-ref --stdin <updates &&
		expect_log main <<-\EOF
		ours
		base
		EOF
	)
'

test_expect_success '--dry-run detects conflicts with modified working tree' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit first &&
		test_commit second modify-me &&
		echo modified >modify-me &&

		git refs list >refs-expect &&
		git diff >diff-expect &&
		test_must_fail git history drop --dry-run HEAD 2>err &&
		test_grep "dropping this commit would overwrite local changes" err &&
		git diff >diff-actual &&
		git refs list >refs-actual &&

		test_cmp diff-expect diff-actual &&
		test_cmp refs-expect refs-actual
	)
'

test_expect_success '--update-refs=head updates only HEAD' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit base &&
		test_commit target &&
		git branch theirs &&
		test_commit ours &&
		git switch theirs &&
		test_commit theirs &&

		# When told to update HEAD only, the command refuses to
		# rewrite commits that are not an ancestor of HEAD.
		test_must_fail git history drop --update-refs=head main 2>err &&
		test_grep "rewritten commit must be an ancestor of HEAD" err &&

		expect_graph --branches <<-\EOF &&
		* theirs
		| * ours
		|/
		* target
		* base
		EOF

		git switch main &&
		git history drop --update-refs=head target &&

		expect_graph --branches <<-\EOF
		* ours
		| * theirs
		| * target
		|/
		* base
		EOF
	)
'

test_expect_success 'conflict with replayed commit aborts cleanly' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		test_commit conflict-a file &&
		test_commit conflict-b file &&

		git refs list >refs-expect &&
		test_must_fail git history drop HEAD~ 2>err &&
		test_grep "failed replaying descendants" err &&
		git refs list >refs-actual &&
		test_cmp refs-expect refs-actual
	)
'

# Build a history where a descendant of the drop target reverts the change
# introduced by the drop target. After dropping, the descendant's diff applies
# against a tree that already lacks the change, so it becomes empty.
setup_empty_descendant_repo () {
	git init "$1" &&
	(
		cd "$1" &&
		echo C1 >file &&
		git add file &&
		git commit -m "base" &&
		git tag base &&
		echo C2 >file &&
		git add file &&
		git commit -m "drop-me" &&
		git tag drop-me &&
		test_commit middle &&
		echo C1 >file &&
		git add file &&
		git commit -m "revert-drop-me" &&
		git tag revert-drop-me
	)
}

test_expect_success '--empty=drop drops descendants that become empty' '
	test_when_finished "rm -rf repo" &&
	setup_empty_descendant_repo repo &&
	(
		cd repo &&

		git history drop --empty=drop drop-me &&

		expect_log <<-\EOF
		middle
		base
		EOF
	)
'

test_expect_success '--empty=keep keeps descendants that become empty' '
	test_when_finished "rm -rf repo" &&
	setup_empty_descendant_repo repo &&
	(
		cd repo &&

		git history drop --empty=keep drop-me &&

		expect_log <<-\EOF &&
		revert-drop-me
		middle
		base
		EOF
		git diff HEAD~ HEAD >diff &&
		test_must_be_empty diff
	)
'

test_expect_success '--empty=abort errors out when a descendant becomes empty' '
	test_when_finished "rm -rf repo" &&
	setup_empty_descendant_repo repo &&
	(
		cd repo &&

		test_must_fail git history drop --empty=abort drop-me 2>err &&
		test_grep "became empty after replay" err
	)
'

test_expect_success 'updates index and worktree when HEAD moves' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&
		test_commit third &&

		git history drop second &&

		# Worktree should no longer contain second.t.
		test_path_is_missing second.t &&
		test_path_is_file first.t &&
		test_path_is_file third.t &&

		# Index and worktree should both match the new HEAD.
		git status --porcelain --untracked-files=no >status &&
		test_must_be_empty status
	)
'

test_expect_success 'updates worktree when dropping HEAD itself' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		test_commit second &&

		git history drop HEAD &&

		test_path_is_missing second.t &&
		test_path_is_file first.t &&

		git status --porcelain --untracked-files=no >status &&
		test_must_be_empty status
	)
'

test_expect_success 'preserves unrelated unstaged modifications' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		echo first-content >unrelated.txt &&
		git add unrelated.txt &&
		git commit -m "add unrelated" &&
		test_commit second &&
		test_commit third &&

		echo locally-modified >unrelated.txt &&

		git diff >diff-expect &&
		git history drop second &&
		git diff >diff-actual &&
		test_cmp diff-expect diff-actual &&
		test_path_is_missing second.t
	)
'

test_expect_success 'preserves unrelated staged changes' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		echo first-content >unrelated.txt &&
		git add unrelated.txt &&
		git commit -m "add unrelated" &&
		test_commit second &&
		test_commit third &&

		echo staged-change >unrelated.txt &&
		git add unrelated.txt &&

		git diff --cached >diff-expect &&
		git history drop second &&
		git diff --cached >diff-actual &&
		test_cmp diff-expect diff-actual &&
		test_path_is_missing second.t
	)
'

test_expect_success 'aborts when local modifications would be overwritten' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		test_commit conflict &&

		echo local-edit >conflict.t &&
		git diff >diff-expect &&
		test_must_fail git history drop HEAD 2>err &&
		test_grep "would overwrite local changes" err &&
		git diff >diff-actual &&
		test_cmp diff-expect diff-actual
	)
'

test_done
