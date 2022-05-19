#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
#

test_description='but rebase assorted tests

This test runs but rebase and checks that the author information is not lost
among other things.
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

GIT_AUTHOR_NAME=author@name
GIT_AUTHOR_EMAIL=bogus@email@address
export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL

test_expect_success 'prepare repository with topic branches' '
	test_cummit "Add A." A First First &&
	but checkout -b force-3way &&
	echo Dummy >Y &&
	but update-index --add Y &&
	but cummit -m "Add Y." &&
	but checkout -b filemove &&
	but reset --soft main &&
	mkdir D &&
	but mv A D/A &&
	but cummit -m "Move A." &&
	but checkout -b my-topic-branch main &&
	test_cummit "Add B." B Second Second &&
	but checkout -f main &&
	echo Third >>A &&
	but update-index A &&
	but cummit -m "Modify A." &&
	but checkout -b side my-topic-branch &&
	echo Side >>C &&
	but add C &&
	but cummit -m "Add C" &&
	but checkout -f my-topic-branch &&
	but tag topic
'

test_expect_success 'rebase on dirty worktree' '
	echo dirty >>A &&
	test_must_fail but rebase main
'

test_expect_success 'rebase on dirty cache' '
	but add A &&
	test_must_fail but rebase main
'

test_expect_success 'rebase against main' '
	but reset --hard HEAD &&
	but rebase main
'

test_expect_success 'rebase sets ORIG_HEAD to pre-rebase state' '
	but checkout -b orig-head topic &&
	pre="$(but rev-parse --verify HEAD)" &&
	but rebase main &&
	test_cmp_rev "$pre" ORIG_HEAD &&
	test_cmp_rev ! "$pre" HEAD
'

test_expect_success 'rebase, with <onto> and <upstream> specified as :/quuxery' '
	test_when_finished "but branch -D torebase" &&
	but checkout -b torebase my-topic-branch^ &&
	upstream=$(but rev-parse ":/Add B") &&
	onto=$(but rev-parse ":/Add A") &&
	but rebase --onto $onto $upstream &&
	but reset --hard my-topic-branch^ &&
	but rebase --onto ":/Add A" ":/Add B" &&
	but checkout my-topic-branch
'

test_expect_success 'the rebase operation should not have destroyed author information' '
	! (but log | grep "Author:" | grep "<>")
'

test_expect_success 'the rebase operation should not have destroyed author information (2)' "
	but log -1 |
	grep 'Author: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>'
"

test_expect_success 'HEAD was detached during rebase' '
	test $(but rev-parse HEAD@{1}) != $(but rev-parse my-topic-branch@{1})
'

test_expect_success 'rebase from ambiguous branch name' '
	but checkout -b topic side &&
	but rebase main
'

test_expect_success 'rebase off of the previous branch using "-"' '
	but checkout main &&
	but checkout HEAD^ &&
	but rebase @{-1} >expect.messages &&
	but merge-base main HEAD >expect.forkpoint &&

	but checkout main &&
	but checkout HEAD^ &&
	but rebase - >actual.messages &&
	but merge-base main HEAD >actual.forkpoint &&

	test_cmp expect.forkpoint actual.forkpoint &&
	# the next one is dubious---we may want to say "-",
	# instead of @{-1}, in the message
	test_cmp expect.messages actual.messages
'

test_expect_success 'rebase a single mode change' '
	but checkout main &&
	but branch -D topic &&
	echo 1 >X &&
	but add X &&
	test_tick &&
	but cummit -m prepare &&
	but checkout -b modechange HEAD^ &&
	echo 1 >X &&
	but add X &&
	test_chmod +x A &&
	test_tick &&
	but cummit -m modechange &&
	GIT_TRACE=1 but rebase main
'

test_expect_success 'rebase is not broken by diff.renames' '
	test_config diff.renames copies &&
	but checkout filemove &&
	GIT_TRACE=1 but rebase force-3way
'

test_expect_success 'setup: recover' '
	test_might_fail but rebase --abort &&
	but reset --hard &&
	but checkout modechange
'

test_expect_success 'Show verbose error when HEAD could not be detached' '
	>B &&
	test_when_finished "rm -f B" &&
	test_must_fail but rebase topic 2>output.err >output.out &&
	test_i18ngrep "The following untracked working tree files would be overwritten by checkout:" output.err &&
	test_i18ngrep B output.err
'

test_expect_success 'fail when upstream arg is missing and not on branch' '
	but checkout topic &&
	test_must_fail but rebase
'

test_expect_success 'fail when upstream arg is missing and not configured' '
	but checkout -b no-config topic &&
	test_must_fail but rebase
'

test_expect_success 'rebase works with format.useAutoBase' '
	test_config format.useAutoBase true &&
	but checkout topic &&
	but rebase main
'

test_expect_success 'default to common base in @{upstream}s reflog if no upstream arg (--merge)' '
	but checkout -b default-base main &&
	but checkout -b default topic &&
	but config branch.default.remote . &&
	but config branch.default.merge refs/heads/default-base &&
	but rebase --merge &&
	but rev-parse --verify default-base >expect &&
	but rev-parse default~1 >actual &&
	test_cmp expect actual &&
	but checkout default-base &&
	but reset --hard HEAD^ &&
	but checkout default &&
	but rebase --merge &&
	but rev-parse --verify default-base >expect &&
	but rev-parse default~1 >actual &&
	test_cmp expect actual
'

test_expect_success 'default to common base in @{upstream}s reflog if no upstream arg (--apply)' '
	but checkout -B default-base main &&
	but checkout -B default topic &&
	but config branch.default.remote . &&
	but config branch.default.merge refs/heads/default-base &&
	but rebase --apply &&
	but rev-parse --verify default-base >expect &&
	but rev-parse default~1 >actual &&
	test_cmp expect actual &&
	but checkout default-base &&
	but reset --hard HEAD^ &&
	but checkout default &&
	but rebase --apply &&
	but rev-parse --verify default-base >expect &&
	but rev-parse default~1 >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-picked cummits and fork-point work together' '
	but checkout default-base &&
	echo Amended >A &&
	but cummit -a --no-edit --amend &&
	test_cummit B B &&
	test_cummit new_B B "New B" &&
	test_cummit C C &&
	but checkout default &&
	but reset --hard default-base@{4} &&
	test_cummit D D &&
	but cherry-pick -2 default-base^ &&
	test_cummit final_B B "Final B" &&
	but rebase &&
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
	but checkout -b quiet topic &&
	but rebase --apply -q main >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'rebase --merge -q is quiet' '
	but checkout -B quiet topic &&
	but rebase --merge -q main >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'Rebase a cummit that sprinkles CRs in' '
	(
		echo "One" &&
		echo "TwoQ" &&
		echo "Three" &&
		echo "FQur" &&
		echo "Five"
	) | q_to_cr >CR &&
	but add CR &&
	test_tick &&
	but cummit -a -m "A file with a line with CR" &&
	but tag file-with-cr &&
	but checkout HEAD^0 &&
	but rebase --onto HEAD^^ HEAD^ &&
	but diff --exit-code file-with-cr:CR HEAD:CR
'

test_expect_success 'rebase can copy notes' '
	but config notes.rewrite.rebase true &&
	but config notes.rewriteRef "refs/notes/*" &&
	test_cummit n1 &&
	test_cummit n2 &&
	test_cummit n3 &&
	but notes add -m"a note" n3 &&
	but rebase --onto n1 n2 &&
	test "a note" = "$(but notes show HEAD)"
'

test_expect_success 'rebase -m can copy notes' '
	but reset --hard n3 &&
	but rebase -m --onto n1 n2 &&
	test "a note" = "$(but notes show HEAD)"
'

test_expect_success 'rebase cummit with an ancient timestamp' '
	but reset --hard &&

	>old.one && but add old.one && test_tick &&
	but cummit --date="@12345 +0400" -m "Old one" &&
	>old.two && but add old.two && test_tick &&
	but cummit --date="@23456 +0500" -m "Old two" &&
	>old.three && but add old.three && test_tick &&
	but cummit --date="@34567 +0600" -m "Old three" &&

	but cat-file commit HEAD^^ >actual &&
	grep "author .* 12345 +0400$" actual &&
	but cat-file commit HEAD^ >actual &&
	grep "author .* 23456 +0500$" actual &&
	but cat-file commit HEAD >actual &&
	grep "author .* 34567 +0600$" actual &&

	but rebase --onto HEAD^^ HEAD^ &&

	but cat-file commit HEAD >actual &&
	grep "author .* 34567 +0600$" actual
'

test_expect_success 'rebase with "From " line in cummit message' '
	but checkout -b preserve-from main~1 &&
	cat >From_.msg <<EOF &&
Somebody embedded an mbox in a cummit message

This is from so-and-so:

From a@b Mon Sep 17 00:00:00 2001
From: John Doe <nobody@example.com>
Date: Sat, 11 Nov 2017 00:00:00 +0000
Subject: not this message

something
EOF
	>From_ &&
	but add From_ &&
	but cummit -F From_.msg &&
	but rebase main &&
	but log -1 --pretty=format:%B >out &&
	test_cmp From_.msg out
'

test_expect_success 'rebase --apply and --show-current-patch' '
	test_create_repo conflict-apply &&
	(
		cd conflict-apply &&
		test_cummit init &&
		echo one >>init.t &&
		but cummit -a -m one &&
		echo two >>init.t &&
		but cummit -a -m two &&
		but tag two &&
		test_must_fail but rebase --apply -f --onto init HEAD^ &&
		GIT_TRACE=1 but rebase --show-current-patch >/dev/null 2>stderr &&
		grep "show.*$(but rev-parse two)" stderr
	)
'

test_expect_success 'rebase --apply and .butattributes' '
	test_create_repo attributes &&
	(
		cd attributes &&
		test_cummit init &&
		but config filter.test.clean "sed -e '\''s/smudged/clean/g'\''" &&
		but config filter.test.smudge "sed -e '\''s/clean/smudged/g'\''" &&

		test_cummit second &&
		but checkout -b test HEAD^ &&

		echo "*.txt filter=test" >.butattributes &&
		but add .butattributes &&
		test_cummit third &&

		echo "This text is smudged." >a.txt &&
		but add a.txt &&
		test_cummit fourth &&

		but checkout -b removal HEAD^ &&
		but rm .butattributes &&
		but add -u &&
		test_cummit fifth &&
		but cherry-pick test &&

		but checkout test &&
		but rebase main &&
		grep "smudged" a.txt &&

		but checkout removal &&
		but reset --hard &&
		but rebase main &&
		grep "clean" a.txt
	)
'

test_expect_success 'rebase--merge.sh and --show-current-patch' '
	test_create_repo conflict-merge &&
	(
		cd conflict-merge &&
		test_cummit init &&
		echo one >>init.t &&
		but cummit -a -m one &&
		echo two >>init.t &&
		but cummit -a -m two &&
		but tag two &&
		test_must_fail but rebase --merge --onto init HEAD^ &&
		but rebase --show-current-patch >actual.patch &&
		GIT_TRACE=1 but rebase --show-current-patch >/dev/null 2>stderr &&
		grep "show.*REBASE_HEAD" stderr &&
		test "$(but rev-parse REBASE_HEAD)" = "$(but rev-parse two)"
	)
'

test_expect_success 'switch to branch checked out here' '
	but checkout main &&
	but rebase main main
'

test_expect_success 'switch to branch not checked out' '
	but checkout main &&
	but branch other &&
	but rebase main other
'

test_expect_success 'switch to non-branch detaches HEAD' '
	but checkout main &&
	old_main=$(but rev-parse HEAD) &&
	but rebase First Second^0 &&
	test_cmp_rev HEAD Second &&
	test_cmp_rev main $old_main &&
	test_must_fail but symbolic-ref HEAD
'

test_expect_success 'refuse to switch to branch checked out elsewhere' '
	but checkout main &&
	but worktree add wt &&
	test_must_fail but -C wt rebase main main 2>err &&
	test_i18ngrep "already checked out" err
'

test_expect_success MINGW,SYMLINKS_WINDOWS 'rebase when .but/logs is a symlink' '
	but checkout main &&
	mv .but/logs actual_logs &&
	cmd //c "mklink /D .but\logs ..\actual_logs" &&
	but rebase -f HEAD^ &&
	test -L .but/logs &&
	rm .but/logs &&
	mv actual_logs .but/logs
'

test_expect_success 'rebase when inside worktree subdirectory' '
	but init main-wt &&
	(
		cd main-wt &&
		but cummit --allow-empty -m "initial" &&
		mkdir -p foo/bar &&
		test_cummit foo/bar/baz &&
		mkdir -p a/b &&
		test_cummit a/b/c &&
		# create another branch for our other worktree
		but branch other &&
		but worktree add ../other-wt other &&
		cd ../other-wt &&
		# create and cd into a subdirectory
		mkdir -p random/dir &&
		cd random/dir &&
		# now do the rebase
		but rebase --onto HEAD^^ HEAD^  # drops the HEAD^ cummit
	)
'

test_done
