#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='git rebase assorted tests

This test runs git rebase and checks that the author information is not lost
among other things.
'
. ./test-lib.sh

GIT_AUTHOR_NAME=author@name
GIT_AUTHOR_EMAIL=bogus@email@address
export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL

test_expect_success 'prepare repository with topic branches' '
	git config core.logAllRefUpdates true &&
	echo First >A &&
	git update-index --add A &&
	git commit -m "Add A." &&
	git checkout -b force-3way &&
	echo Dummy >Y &&
	git update-index --add Y &&
	git commit -m "Add Y." &&
	git checkout -b filemove &&
	git reset --soft master &&
	mkdir D &&
	git mv A D/A &&
	git commit -m "Move A." &&
	git checkout -b my-topic-branch master &&
	echo Second >B &&
	git update-index --add B &&
	git commit -m "Add B." &&
	git checkout -f master &&
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
	test_must_fail git rebase master
'

test_expect_success 'rebase on dirty cache' '
	git add A &&
	test_must_fail git rebase master
'

test_expect_success 'rebase against master' '
	git reset --hard HEAD &&
	git rebase master
'

test_expect_success 'rebase sets ORIG_HEAD to pre-rebase state' '
	git checkout -b orig-head topic &&
	pre="$(git rev-parse --verify HEAD)" &&
	git rebase master &&
	test_cmp_rev "$pre" ORIG_HEAD &&
	! test_cmp_rev "$pre" HEAD
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
	git rebase master
'

test_expect_success 'rebase off of the previous branch using "-"' '
	git checkout master &&
	git checkout HEAD^ &&
	git rebase @{-1} >expect.messages &&
	git merge-base master HEAD >expect.forkpoint &&

	git checkout master &&
	git checkout HEAD^ &&
	git rebase - >actual.messages &&
	git merge-base master HEAD >actual.forkpoint &&

	test_cmp expect.forkpoint actual.forkpoint &&
	# the next one is dubious---we may want to say "-",
	# instead of @{-1}, in the message
	test_i18ncmp expect.messages actual.messages
'

test_expect_success 'rebase a single mode change' '
	git checkout master &&
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
	GIT_TRACE=1 git rebase master
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
	test_must_fail git rebase topic 2>output.err >output.out &&
	test_i18ngrep "The following untracked working tree files would be overwritten by checkout:" output.err &&
	test_i18ngrep B output.err
'
rm -f B

test_expect_success 'fail when upstream arg is missing and not on branch' '
	git checkout topic &&
	test_must_fail git rebase
'

test_expect_success 'fail when upstream arg is missing and not configured' '
	git checkout -b no-config topic &&
	test_must_fail git rebase
'

test_expect_success 'default to common base in @{upstream}s reflog if no upstream arg' '
	git checkout -b default-base master &&
	git checkout -b default topic &&
	git config branch.default.remote . &&
	git config branch.default.merge refs/heads/default-base &&
	git rebase &&
	git rev-parse --verify default-base >expect &&
	git rev-parse default~1 >actual &&
	test_cmp expect actual &&
	git checkout default-base &&
	git reset --hard HEAD^ &&
	git checkout default &&
	git rebase &&
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

test_expect_success 'rebase -q is quiet' '
	git checkout -b quiet topic &&
	git rebase -q master >output.out 2>&1 &&
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
	git checkout -b preserve-from master~1 &&
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
	git rebase master &&
	git log -1 --pretty=format:%B >out &&
	test_cmp From_.msg out
'

test_expect_success 'rebase --am and --show-current-patch' '
	test_create_repo conflict-apply &&
	(
		cd conflict-apply &&
		test_commit init &&
		echo one >>init.t &&
		git commit -a -m one &&
		echo two >>init.t &&
		git commit -a -m two &&
		git tag two &&
		test_must_fail git rebase -f --onto init HEAD^ &&
		GIT_TRACE=1 git rebase --show-current-patch >/dev/null 2>stderr &&
		grep "show.*$(git rev-parse two)" stderr
	)
'

test_expect_success 'rebase --am and .gitattributes' '
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
		git rebase master &&
		grep "smudged" a.txt &&

		git checkout removal &&
		git reset --hard &&
		git rebase master &&
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

test_expect_success 'rebase -c rebase.useBuiltin=false warning' '
	expected="rebase.useBuiltin support has been removed" &&

	# Only warn when the legacy rebase is requested...
	test_must_fail git -c rebase.useBuiltin=false rebase 2>err &&
	test_i18ngrep "$expected" err &&
	test_must_fail env GIT_TEST_REBASE_USE_BUILTIN=false git rebase 2>err &&
	test_i18ngrep "$expected" err &&

	# ...not when we would have used the built-in anyway
	test_must_fail git -c rebase.useBuiltin=true rebase 2>err &&
	test_must_be_empty err &&
	test_must_fail env GIT_TEST_REBASE_USE_BUILTIN=true git rebase 2>err &&
	test_must_be_empty err
'

test_done
