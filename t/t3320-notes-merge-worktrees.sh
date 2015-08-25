#!/bin/sh
#
# Copyright (c) 2015 Twitter, Inc
#

test_description='Test merging of notes trees in multiple worktrees'

. ./test-lib.sh

test_expect_success 'setup commit' '
	test_commit tantrum
'

commit_tantrum=$(git rev-parse tantrum^{commit})

test_expect_success 'setup notes ref (x)' '
	git config core.notesRef refs/notes/x &&
	git notes add -m "x notes on tantrum" tantrum
'

test_expect_success 'setup local branch (y)' '
	git update-ref refs/notes/y refs/notes/x &&
	git config core.notesRef refs/notes/y &&
	git notes remove tantrum
'

test_expect_success 'setup remote branch (z)' '
	git update-ref refs/notes/z refs/notes/x &&
	git config core.notesRef refs/notes/z &&
	git notes add -f -m "conflicting notes on tantrum" tantrum
'

test_expect_success 'modify notes ref ourselves (x)' '
	git config core.notesRef refs/notes/x &&
	git notes add -f -m "more conflicting notes on tantrum" tantrum
'

test_expect_success 'create some new worktrees' '
	git worktree add -b newbranch worktree master &&
	git worktree add -b newbranch2 worktree2 master
'

test_expect_success 'merge z into y fails and sets NOTES_MERGE_REF' '
	git config core.notesRef refs/notes/y &&
	test_must_fail git notes merge z &&
	echo "ref: refs/notes/y" >expect &&
	test_cmp .git/NOTES_MERGE_REF expect
'

test_expect_success 'merge z into y while mid-merge in another workdir fails' '
	(
		cd worktree &&
		git config core.notesRef refs/notes/y &&
		test_must_fail git notes merge z 2>err &&
		grep "A notes merge into refs/notes/y is already in-progress at" err
	) &&
	test_path_is_missing .git/worktrees/worktree/NOTES_MERGE_REF
'

test_expect_success 'merge z into x while mid-merge on y succeeds' '
	(
		cd worktree2 &&
		git config core.notesRef refs/notes/x &&
		test_must_fail git notes merge z 2>&1 >out &&
		grep "Automatic notes merge failed" out &&
		grep -v "A notes merge into refs/notes/x is already in-progress in" out
	) &&
	echo "ref: refs/notes/x" >expect &&
	test_cmp .git/worktrees/worktree2/NOTES_MERGE_REF expect
'

test_done
