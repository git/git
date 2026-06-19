#!/bin/sh

test_description='tests for git-history fixup subcommand'

. ./test-lib.sh

fixup_with_message () {
	cat >message &&
	write_script fake-editor.sh <<-\EOF &&
	cp message "$1"
	EOF
	test_set_editor "$(pwd)"/fake-editor.sh &&
	git history fixup --reedit-message "$@" &&
	rm fake-editor.sh message
}

expect_changes () {
	git log --format="%s" --numstat "$@" >actual.raw &&
	sed '/^$/d' <actual.raw >actual &&
	cat >expect &&
	test_cmp expect actual
}

test_expect_success 'errors on missing commit argument' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_must_fail git history fixup 2>err &&
		test_grep "command expects a single revision" err
	)
'

test_expect_success 'errors on too many arguments' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_must_fail git history fixup HEAD HEAD 2>err &&
		test_grep "command expects a single revision" err
	)
'

test_expect_success 'errors on unknown revision' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_must_fail git history fixup does-not-exist 2>err &&
		test_grep "commit cannot be found: does-not-exist" err
	)
'

test_expect_success 'errors when nothing is staged' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test_must_fail git history fixup HEAD 2>err &&
		test_grep "nothing to fixup: no staged changes" err
	)
'

test_expect_success 'errors in a bare repository' '
	test_when_finished "rm -rf repo repo.git" &&
	git init repo &&
	test_commit -C repo initial &&
	git clone --bare repo repo.git &&
	test_must_fail git -C repo.git history fixup HEAD 2>err &&
	test_grep "cannot run fixup in a bare repository" err
'

test_expect_success 'errors with invalid --empty= value' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_must_fail git -C repo history fixup --empty=bogus HEAD 2>err &&
	test_grep "unrecognized.*--empty.*bogus" err
'

test_expect_success 'can fixup the tip commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		echo content >file.txt &&
		git add file.txt &&
		git commit -m "add file" &&

		echo fix >>file.txt &&
		git add file.txt &&

		expect_changes <<-\EOF &&
		add file
		1	0	file.txt
		initial
		1	0	initial.t
		EOF

		git symbolic-ref HEAD >branch-expect &&
		git history fixup HEAD &&
		git symbolic-ref HEAD >branch-actual &&
		test_cmp branch-expect branch-actual &&

		expect_changes <<-\EOF &&
		add file
		2	0	file.txt
		initial
		1	0	initial.t
		EOF

		# Verify the fix is in the tip commit tree
		git show HEAD:file.txt >actual &&
		printf "content\nfix\n" >expect &&
		test_cmp expect actual &&

		git reflog >reflog &&
		test_grep "fixup: updating HEAD" reflog
	)
'

test_expect_success 'can fixup a commit in the middle of history' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit first &&
		echo content >file.txt &&
		git add file.txt &&
		git commit -m "add file" &&
		test_commit third &&

		echo fix >>file.txt &&
		git add file.txt &&

		expect_changes <<-\EOF &&
		third
		1	0	third.t
		add file
		1	0	file.txt
		first
		1	0	first.t
		EOF

		git history fixup HEAD~ &&

		expect_changes <<-\EOF &&
		third
		1	0	third.t
		add file
		2	0	file.txt
		first
		1	0	first.t
		EOF

		# Verify the fix landed in the "add file" commit.
		git show HEAD~:file.txt >actual &&
		printf "content\nfix\n" >expect &&
		test_cmp expect actual &&

		# And verify that the replayed commit also has the change.
		git show HEAD:file.txt >actual &&
		printf "content\nfix\n" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'can fixup root commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		echo initial >root.txt &&
		git add root.txt &&
		git commit -m "root" &&
		test_commit second &&

		expect_changes <<-\EOF &&
		second
		1	0	second.t
		root
		1	0	root.txt
		EOF

		echo fix >>root.txt &&
		git add root.txt &&
		git history fixup HEAD~ &&

		expect_changes <<-\EOF &&
		second
		1	0	second.t
		root
		2	0	root.txt
		EOF

		git show HEAD~:root.txt >actual &&
		printf "initial\nfix\n" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'preserves commit message and authorship' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		echo content >file.txt &&
		git add file.txt &&
		git commit --author="Original <original@example.com>" -m "original message" &&

		echo fix >>file.txt &&
		git add file.txt &&
		git history fixup HEAD &&

		# Message preserved
		git log -1 --format="%s" >actual &&
		echo "original message" >expect &&
		test_cmp expect actual &&

		# Authorship preserved
		git log -1 --format="%an <%ae>" >actual &&
		echo "Original <original@example.com>" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'updates all descendant branches by default' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit base &&
		git branch branch &&
		test_commit ours &&
		git switch branch &&
		test_commit theirs &&
		git switch main &&

		echo fix >fix.txt &&
		git add fix.txt &&
		git history fixup base &&

		expect_changes --branches <<-\EOF &&
		theirs
		1	0	theirs.t
		ours
		1	0	ours.t
		base
		1	0	base.t
		1	0	fix.txt
		EOF

		# Both branches should have the fix in the base
		git show main~:fix.txt >actual &&
		echo fix >expect &&
		test_cmp expect actual &&
		git show branch~:fix.txt >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'can fixup commit on a different branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&
		git branch theirs &&
		test_commit ours &&
		git switch theirs &&
		test_commit theirs &&

		# Stage a change while on "theirs"
		echo fix >fix.txt &&
		git add fix.txt &&

		# Ensure that "ours" does not change, as it does not contain
		# the commit in question.
		git rev-parse ours >ours-before &&
		git history fixup theirs &&
		git rev-parse ours >ours-after &&
		test_cmp ours-before ours-after &&

		git show HEAD:fix.txt >actual &&
		echo fix >expect &&
		test_cmp expect actual
	)
'

test_expect_success '--dry-run prints ref updates without modifying repo' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit base &&
		git branch branch &&
		test_commit main-tip &&
		git switch branch &&
		test_commit branch-tip &&
		git switch main &&

		echo fix >fix.txt &&
		git add fix.txt &&

		git refs list >refs-before &&
		git history fixup --dry-run base >updates &&
		git refs list >refs-after &&
		test_cmp refs-before refs-after &&

		test_grep "update refs/heads/main" updates &&
		test_grep "update refs/heads/branch" updates &&

		expect_changes --branches <<-\EOF &&
		branch-tip
		1	0	branch-tip.t
		main-tip
		1	0	main-tip.t
		base
		1	0	base.t
		EOF

		git update-ref --stdin <updates &&
		expect_changes --branches <<-\EOF
		branch-tip
		1	0	branch-tip.t
		main-tip
		1	0	main-tip.t
		base
		1	0	base.t
		1	0	fix.txt
		EOF
	)
'

test_expect_success '--update-refs=head updates only HEAD' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit base &&
		git branch branch &&
		test_commit main-tip &&
		git switch branch &&
		test_commit branch-tip &&

		echo fix >fix.txt &&
		git add fix.txt &&

		# Only HEAD (branch) should be updated
		git history fixup --update-refs=head base &&

		# The main branch should be unaffected.
		expect_changes main <<-\EOF &&
		main-tip
		1	0	main-tip.t
		base
		1	0	base.t
		EOF

		# But the currently checked out branch should be modified.
		expect_changes branch <<-\EOF
		branch-tip
		1	0	branch-tip.t
		base
		1	0	base.t
		1	0	fix.txt
		EOF
	)
'

test_expect_success '--update-refs=head refuses to rewrite commits not in HEAD ancestry' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&
		test_commit base &&
		git branch other &&
		test_commit main-tip &&
		git switch other &&
		test_commit other-tip &&

		echo fix >fix.txt &&
		git add fix.txt &&

		test_must_fail git history fixup --update-refs=head main-tip 2>err &&
		test_grep "rewritten commit must be an ancestor of HEAD" err
	)
'

test_expect_success 'aborts when fixup would produce conflicts' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "line one" >file.txt &&
		git add file.txt &&
		git commit -m "first" &&

		echo "line two" >file.txt &&
		git add file.txt &&
		git commit -m "second" &&

		echo "conflicting change" >file.txt &&
		git add file.txt &&

		git refs list >refs-before &&
		test_must_fail git history fixup HEAD~ 2>err &&
		test_grep "fixup would produce conflicts" err &&
		git refs list >refs-after &&
		test_cmp refs-before refs-after
	)
'

test_expect_success '--reedit-message opens editor for the commit message' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		echo content >file.txt &&
		git add file.txt &&
		git commit -m "add file" &&

		echo fix >>file.txt &&
		git add file.txt &&

		fixup_with_message HEAD <<-\EOF &&
		add file with fix
		EOF

		expect_changes --branches <<-\EOF
		add file with fix
		2	0	file.txt
		initial
		1	0	initial.t
		EOF
	)
'

test_expect_success 'retains unstaged working tree changes after fixup' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		touch a b &&
		git add . &&
		git commit -m "initial commit" &&
		echo staged >a &&
		echo unstaged >b &&
		git add a &&
		git history fixup HEAD &&

		# b is still modified in the worktree but not staged
		cat >expect <<-\EOF &&
		 M b
		EOF
		git status --porcelain --untracked-files=no >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'index is clean after fixup when target is HEAD' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit initial &&
		echo fix >fix.txt &&
		git add fix.txt &&
		git history fixup HEAD &&

		git status --porcelain --untracked-files=no >actual &&
		test_must_be_empty actual
	)
'

test_expect_success 'index is unchanged on conflict' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo base >file.txt &&
		git add file.txt &&
		git commit -m base &&
		echo change >file.txt &&
		git add file.txt &&
		git commit -m change &&

		echo conflict >file.txt &&
		git add file.txt &&

		git diff --cached >index-before &&
		test_must_fail git history fixup HEAD~ &&
		git diff --cached >index-after &&
		test_cmp index-before index-after
	)
'

test_expect_success '--empty=drop removes target commit and replays descendants onto its parent' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&

		test_commit first &&
		test_commit second &&
		test_commit third &&

		git rm second.t &&
		git history fixup --empty=drop HEAD~ &&

		expect_changes <<-\EOF &&
		third
		1	0	third.t
		first
		1	0	first.t
		EOF
		test_must_fail git show HEAD:second.t
	)
'

test_expect_success '--empty=drop errors out when dropping the root commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit first &&
		test_commit second &&

		git rm first.t &&
		test_must_fail git history fixup --empty=drop HEAD~ 2>err &&
		test_grep "cannot drop root commit" err
	)
'

test_expect_success '--empty=drop can drop the HEAD commit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit first &&
		test_commit second &&

		git rm second.t &&
		git history fixup --empty=drop HEAD &&

		expect_changes <<-\EOF
		first
		1	0	first.t
		EOF
	)
'

test_expect_success '--empty=drop drops empty replayed commits' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		touch base remove-me &&
		git add . &&
		git commit -m "base" &&
		git rm remove-me &&
		git commit -m "remove" &&
		touch reintroduce remove-me &&
		git add . &&
		git commit -m "reintroduce" &&

		git rm remove-me &&
		git history fixup --empty=drop HEAD~2 &&

		expect_changes <<-\EOF
		reintroduce
		0	0	reintroduce
		0	0	remove-me
		base
		0	0	base
		EOF
	)
'

test_expect_success '--empty=keep keeps commit when fixup target becomes empty' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit first &&
		test_commit second &&
		test_commit third &&

		git rm second.t &&
		git history fixup --empty=keep HEAD~ &&

		expect_changes <<-\EOF
		third
		1	0	third.t
		second
		first
		1	0	first.t
		EOF
	)
'

test_expect_success '--empty=keep keeps commit when replayed commit becomes empty' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		touch base remove-me &&
		git add . &&
		git commit -m "base" &&
		git rm remove-me &&
		git commit -m "remove" &&
		touch reintroduce remove-me &&
		git add . &&
		git commit -m "reintroduce" &&

		git rm remove-me &&
		git history fixup --empty=keep HEAD~2 &&

		expect_changes <<-\EOF
		reintroduce
		0	0	reintroduce
		0	0	remove-me
		remove
		base
		0	0	base
		EOF
	)
'

test_expect_success '--empty=abort errors out when fixup target becomes empty' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit first &&
		test_commit second &&

		git rm first.t &&
		test_must_fail git history fixup --empty=abort HEAD~ 2>err &&
		test_grep "fixup makes commit.*empty" err
	)
'

test_expect_success '--empty=abort errors out when a descendant becomes empty during replay' '
	test_when_finished "rm -rf repo" &&
	git init repo --initial-branch=main &&
	(
		cd repo &&

		touch base remove-me &&
		git add . &&
		git commit -m "base" &&
		git rm remove-me &&
		git commit -m "remove" &&
		touch reintroduce remove-me &&
		git add . &&
		git commit -m "reintroduce" &&

		git rm remove-me &&
		test_must_fail git history fixup --empty=abort HEAD~2 2>err &&
		test_grep "became empty after replay" err
	)
'

test_done
