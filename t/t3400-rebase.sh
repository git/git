#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='git rebase assorted tests

This test runs git rebase and checks that the author information is not lost
among other things.
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

GIT_AUTHOR_NAME=author@name
GIT_AUTHOR_EMAIL=bogus@email@address
export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL

test_expect_success 'prepare repository with topic branches' '
	test_commit "Add A." A First First &&
	git checkout -b force-3way &&
	echo Dummy >Y &&
	git update-index --add Y &&
	git commit -m "Add Y." &&
	git checkout -b filemove &&
	git reset --soft main &&
	mkdir D &&
	git mv A D/A &&
	git commit -m "Move A." &&
	git checkout -b my-topic-branch main &&
	test_commit "Add B." B Second Second &&
	git checkout -f main &&
	echo Third >>A &&
	git update-index A &&
	git commit -m "Modify A." &&
	git checkout -b side my-topic-branch &&
	echo Side >>C &&
	git add C &&
	git commit -m "Add C" &&
	git checkout -f my-topic-branch &&
	git tag topic
'

test_expect_success 'rebase on dirty worktree' '
	echo dirty >>A &&
	test_must_fail git rebase main
'

test_expect_success 'rebase on dirty cache' '
	git add A &&
	test_must_fail git rebase main
'

test_expect_success 'rebase against main' '
	git reset --hard HEAD &&
	git rebase main
'

test_expect_success 'rebase sets ORIG_HEAD to pre-rebase state' '
	git checkout -b orig-head topic &&
	pre="$(git rev-parse --verify HEAD)" &&
	git rebase main &&
	test_cmp_rev "$pre" ORIG_HEAD &&
	test_cmp_rev ! "$pre" HEAD
'

test_expect_success 'rebase, with <onto> and <upstream> specified as :/quuxery' '
	test_when_finished "git branch -D torebase" &&
	git checkout -b torebase my-topic-branch^ &&
	upstream=$(git rev-parse ":/Add B") &&
	onto=$(git rev-parse ":/Add A") &&
	git rebase --onto $onto $upstream &&
	git reset --hard my-topic-branch^ &&
	git rebase --onto ":/Add A" ":/Add B" &&
	git checkout my-topic-branch
'

test_expect_success 'the rebase operation should not have destroyed author information' '
	! (git log | grep "Author:" | grep "<>")
'

test_expect_success 'the rebase operation should not have destroyed author information (2)' "
	git log -1 |
	grep 'Author: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>'
"

test_expect_success 'HEAD was detached during rebase' '
	test $(git rev-parse HEAD@{1}) != $(git rev-parse my-topic-branch@{1})
'

test_expect_success 'rebase from ambiguous branch name' '
	git checkout -b topic side &&
	git rebase main
'

test_expect_success 'rebase off of the previous branch using "-"' '
	git checkout main &&
	git checkout HEAD^ &&
	git rebase @{-1} >expect.messages &&
	git merge-base main HEAD >expect.forkpoint &&

	git checkout main &&
	git checkout HEAD^ &&
	git rebase - >actual.messages &&
	git merge-base main HEAD >actual.forkpoint &&

	test_cmp expect.forkpoint actual.forkpoint &&
	# the next one is dubious---we may want to say "-",
	# instead of @{-1}, in the message
	test_cmp expect.messages actual.messages
'

test_expect_success 'rebase a single mode change' '
	git checkout main &&
	git branch -D topic &&
	echo 1 >X &&
	git add X &&
	test_tick &&
	git commit -m prepare &&
	git checkout -b modechange HEAD^ &&
	echo 1 >X &&
	git add X &&
	test_chmod +x A &&
	test_tick &&
	git commit -m modechange &&
	GIT_TRACE=1 git rebase main
'

test_expect_success 'rebase is not broken by diff.renames' '
	test_config diff.renames copies &&
	git checkout filemove &&
	GIT_TRACE=1 git rebase force-3way
'

test_expect_success 'setup: recover' '
	test_might_fail git rebase --abort &&
	git reset --hard &&
	git checkout modechange
'

test_expect_success 'Show verbose error when HEAD could not be detached' '
	>B &&
	test_when_finished "rm -f B" &&
	test_must_fail git rebase topic 2>output.err >output.out &&
	test_i18ngrep "The following untracked working tree files would be overwritten by checkout:" output.err &&
	test_i18ngrep B output.err
'

test_expect_success 'fail when upstream arg is missing and not on branch' '
	git checkout topic &&
	test_must_fail git rebase
'

test_expect_success 'fail when upstream arg is missing and not configured' '
	git checkout -b no-config topic &&
	test_must_fail git rebase
'

test_expect_success 'rebase works with format.useAutoBase' '
	test_config format.useAutoBase true &&
	git checkout topic &&
	git rebase main
'

test_expect_success 'default to common base in @{upstream}s reflog if no upstream arg (--merge)' '
	git checkout -b default-base main &&
	git checkout -b default topic &&
	git config branch.default.remote . &&
	git config branch.default.merge refs/heads/default-base &&
	git rebase --merge &&
	git rev-parse --verify default-base >expect &&
	git rev-parse default~1 >actual &&
	test_cmp expect actual &&
	git checkout default-base &&
	git reset --hard HEAD^ &&
	git checkout default &&
	git rebase --merge &&
	git rev-parse --verify default-base >expect &&
	git rev-parse default~1 >actual &&
	test_cmp expect actual
'

test_expect_success 'default to common base in @{upstream}s reflog if no upstream arg (--apply)' '
	git checkout -B default-base main &&
	git checkout -B default topic &&
	git config branch.default.remote . &&
	git config branch.default.merge refs/heads/default-base &&
	git rebase --apply &&
	git rev-parse --verify default-base >expect &&
	git rev-parse default~1 >actual &&
	test_cmp expect actual &&
	git checkout default-base &&
	git reset --hard HEAD^ &&
	git checkout default &&
	git rebase --apply &&
	git rev-parse --verify default-base >expect &&
	git rev-parse default~1 >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-picked commits and fork-point work together' '
	git checkout default-base &&
	echo Amended >A &&
	git commit -a --no-edit --amend &&
	test_commit B B &&
	test_commit new_B B "New B" &&
	test_commit C C &&
	git checkout default &&
	git reset --hard default-base@{4} &&
	test_commit D D &&
	git cherry-pick -2 default-base^ &&
	test_commit final_B B "Final B" &&
	git rebase &&
	echo Amended >expect &&
	test_cmp expect A &&
	echo "Final B" >expect &&
	test_cmp expect B &&
	echo C >expect &&
	test_cmp expect C &&
	echo D >expect &&
	test_cmp expect D
'

test_expect_success 'rebase --apply -q is quiet' '
	git checkout -b quiet topic &&
	git rebase --apply -q main >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'rebase --merge -q is quiet' '
	git checkout -B quiet topic &&
	git rebase --merge -q main >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'Rebase a commit that sprinkles CRs in' '
	(
		echo "One" &&
		echo "TwoQ" &&
		echo "Three" &&
		echo "FQur" &&
		echo "Five"
	) | q_to_cr >CR &&
	git add CR &&
	test_tick &&
	git commit -a -m "A file with a line with CR" &&
	git tag file-with-cr &&
	git checkout HEAD^0 &&
	git rebase --onto HEAD^^ HEAD^ &&
	git diff --exit-code file-with-cr:CR HEAD:CR
'

test_expect_success 'rebase can copy notes' '
	git config notes.rewrite.rebase true &&
	git config notes.rewriteRef "refs/notes/*" &&
	test_commit n1 &&
	test_commit n2 &&
	test_commit n3 &&
	git notes add -m"a note" n3 &&
	git rebase --onto n1 n2 &&
	test "a note" = "$(git notes show HEAD)"
'

test_expect_success 'rebase -m can copy notes' '
	git reset --hard n3 &&
	git rebase -m --onto n1 n2 &&
	test "a note" = "$(git notes show HEAD)"
'

test_expect_success 'rebase commit with an ancient timestamp' '
	git reset --hard &&

	>old.one && git add old.one && test_tick &&
	git commit --date="@12345 +0400" -m "Old one" &&
	>old.two && git add old.two && test_tick &&
	git commit --date="@23456 +0500" -m "Old two" &&
	>old.three && git add old.three && test_tick &&
	git commit --date="@34567 +0600" -m "Old three" &&

	git cat-file commit HEAD^^ >actual &&
	grep "author .* 12345 +0400$" actual &&
	git cat-file commit HEAD^ >actual &&
	grep "author .* 23456 +0500$" actual &&
	git cat-file commit HEAD >actual &&
	grep "author .* 34567 +0600$" actual &&

	git rebase --onto HEAD^^ HEAD^ &&

	git cat-file commit HEAD >actual &&
	grep "author .* 34567 +0600$" actual
'

test_expect_success 'rebase with "From " line in commit message' '
	git checkout -b preserve-from main~1 &&
	cat >From_.msg <<EOF &&
Somebody embedded an mbox in a commit message

This is from so-and-so:

From a@b Mon Sep 17 00:00:00 2001
From: John Doe <nobody@example.com>
Date: Sat, 11 Nov 2017 00:00:00 +0000
Subject: not this message

something
EOF
	>From_ &&
	git add From_ &&
	git commit -F From_.msg &&
	git rebase main &&
	git log -1 --pretty=format:%B >out &&
	test_cmp From_.msg out
'

test_expect_success 'rebase --apply and --show-current-patch' '
	test_create_repo conflict-apply &&
	(
		cd conflict-apply &&
		test_commit init &&
		echo one >>init.t &&
		git commit -a -m one &&
		echo two >>init.t &&
		git commit -a -m two &&
		git tag two &&
		test_must_fail git rebase --apply -f --onto init HEAD^ &&
		GIT_TRACE=1 git rebase --show-current-patch >/dev/null 2>stderr &&
		grep "show.*$(git rev-parse two)" stderr
	)
'

test_expect_success 'rebase --apply and .gitattributes' '
	test_create_repo attributes &&
	(
		cd attributes &&
		test_commit init &&
		git config filter.test.clean "sed -e '\''s/smudged/clean/g'\''" &&
		git config filter.test.smudge "sed -e '\''s/clean/smudged/g'\''" &&

		test_commit second &&
		git checkout -b test HEAD^ &&

		echo "*.txt filter=test" >.gitattributes &&
		git add .gitattributes &&
		test_commit third &&

		echo "This text is smudged." >a.txt &&
		git add a.txt &&
		test_commit fourth &&

		git checkout -b removal HEAD^ &&
		git rm .gitattributes &&
		git add -u &&
		test_commit fifth &&
		git cherry-pick test &&

		git checkout test &&
		git rebase main &&
		grep "smudged" a.txt &&

		git checkout removal &&
		git reset --hard &&
		git rebase main &&
		grep "clean" a.txt
	)
'

test_expect_success 'rebase--merge.sh and --show-current-patch' '
	test_create_repo conflict-merge &&
	(
		cd conflict-merge &&
		test_commit init &&
		echo one >>init.t &&
		git commit -a -m one &&
		echo two >>init.t &&
		git commit -a -m two &&
		git tag two &&
		test_must_fail git rebase --merge --onto init HEAD^ &&
		git rebase --show-current-patch >actual.patch &&
		GIT_TRACE=1 git rebase --show-current-patch >/dev/null 2>stderr &&
		grep "show.*REBASE_HEAD" stderr &&
		test "$(git rev-parse REBASE_HEAD)" = "$(git rev-parse two)"
	)
'

test_expect_success 'switch to branch checked out here' '
	git checkout main &&
	git rebase main main
'

test_expect_success 'switch to branch checked out elsewhere fails' '
	test_when_finished "
		git worktree remove wt1 &&
		git worktree remove wt2 &&
		git branch -d shared
	" &&
	git worktree add wt1 -b shared &&
	git worktree add wt2 -f shared &&
	# we test in both worktrees to ensure that works
	# as expected with "first" and "next" worktrees
	test_must_fail git -C wt1 rebase shared shared &&
	test_must_fail git -C wt2 rebase shared shared
'

test_expect_success 'switch to branch not checked out' '
	git checkout main &&
	git branch other &&
	git rebase main other
'

test_expect_success 'switch to non-branch detaches HEAD' '
	git checkout main &&
	old_main=$(git rev-parse HEAD) &&
	git rebase First Second^0 &&
	test_cmp_rev HEAD Second &&
	test_cmp_rev main $old_main &&
	test_must_fail git symbolic-ref HEAD
'

test_expect_success 'refuse to switch to branch checked out elsewhere' '
	git checkout main &&
	git worktree add wt &&
	test_must_fail git -C wt rebase main main 2>err &&
	test_i18ngrep "already checked out" err
'

test_expect_success MINGW,SYMLINKS_WINDOWS 'rebase when .git/logs is a symlink' '
	git checkout main &&
	mv .git/logs actual_logs &&
	cmd //c "mklink /D .git\logs ..\actual_logs" &&
	git rebase -f HEAD^ &&
	test -L .git/logs &&
	rm .git/logs &&
	mv actual_logs .git/logs
'

test_expect_success 'rebase when inside worktree subdirectory' '
	git init main-wt &&
	(
		cd main-wt &&
		git commit --allow-empty -m "initial" &&
		mkdir -p foo/bar &&
		test_commit foo/bar/baz &&
		mkdir -p a/b &&
		test_commit a/b/c &&
		# create another branch for our other worktree
		git branch other &&
		git worktree add ../other-wt other &&
		cd ../other-wt &&
		# create and cd into a subdirectory
		mkdir -p random/dir &&
		cd random/dir &&
		# now do the rebase
		git rebase --onto HEAD^^ HEAD^  # drops the HEAD^ commit
	)
'

test_done
