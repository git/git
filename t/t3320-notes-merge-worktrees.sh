#!/bin/sh
#
# Copyright (c) 2015 Twitter, Inc
#

test_description='Test merging of notes trees in multiple worktrees'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup cummit' '
	test_cummit tantrum
'

cummit_tantrum=$(but rev-parse tantrum^{cummit})

test_expect_success 'setup notes ref (x)' '
	but config core.notesRef refs/notes/x &&
	but notes add -m "x notes on tantrum" tantrum
'

test_expect_success 'setup local branch (y)' '
	but update-ref refs/notes/y refs/notes/x &&
	but config core.notesRef refs/notes/y &&
	but notes remove tantrum
'

test_expect_success 'setup remote branch (z)' '
	but update-ref refs/notes/z refs/notes/x &&
	but config core.notesRef refs/notes/z &&
	but notes add -f -m "conflicting notes on tantrum" tantrum
'

test_expect_success 'modify notes ref ourselves (x)' '
	but config core.notesRef refs/notes/x &&
	but notes add -f -m "more conflicting notes on tantrum" tantrum
'

test_expect_success 'create some new worktrees' '
	but worktree add -b newbranch worktree main &&
	but worktree add -b newbranch2 worktree2 main
'

test_expect_success 'merge z into y fails and sets NOTES_MERGE_REF' '
	but config core.notesRef refs/notes/y &&
	test_must_fail but notes merge z &&
	echo "refs/notes/y" >expect &&
	but symbolic-ref NOTES_MERGE_REF >actual &&
	test_cmp expect actual
'

test_expect_success 'merge z into y while mid-merge in another workdir fails' '
	(
		cd worktree &&
		but config core.notesRef refs/notes/y &&
		test_must_fail but notes merge z 2>err &&
		test_i18ngrep "a notes merge into refs/notes/y is already in-progress at" err
	) &&
	test_must_fail but -C worktree symbolic-ref NOTES_MERGE_REF
'

test_expect_success 'merge z into x while mid-merge on y succeeds' '
	(
		cd worktree2 &&
		but config core.notesRef refs/notes/x &&
		test_must_fail but notes merge z >out 2>&1 &&
		test_i18ngrep "Automatic notes merge failed" out &&
		grep -v "A notes merge into refs/notes/x is already in-progress in" out
	) &&
	echo "refs/notes/x" >expect &&
	but -C worktree2 symbolic-ref NOTES_MERGE_REF >actual &&
	test_cmp expect actual
'

test_done
