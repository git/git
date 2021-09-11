#!/bin/sh

test_description='Test handling of overwriting untracked files'

. ./test-lib.sh

test_setup_reset () {
	test_create_repo reset_$1 &&
	(
		cd reset_$1 &&
		test_commit init &&

		git branch stable &&
		git branch work &&

		git checkout work &&
		test_commit foo &&

		git checkout stable
	)
}

test_expect_success 'reset --hard will nuke untracked files/dirs' '
	test_setup_reset hard &&
	(
		cd reset_hard &&
		git ls-tree -r stable &&
		git log --all --name-status --oneline &&
		git ls-tree -r work &&

		mkdir foo.t &&
		echo precious >foo.t/file &&
		echo foo >expect &&

		git reset --hard work &&

		# check that untracked directory foo.t/ was nuked
		test_path_is_file foo.t &&
		test_cmp expect foo.t
	)
'

test_expect_success 'reset --merge will preserve untracked files/dirs' '
	test_setup_reset merge &&
	(
		cd reset_merge &&

		mkdir foo.t &&
		echo precious >foo.t/file &&
		cp foo.t/file expect &&

		test_must_fail git reset --merge work 2>error &&
		test_cmp expect foo.t/file &&
		grep "Updating.*foo.t.*would lose untracked files" error
	)
'

test_expect_success 'reset --keep will preserve untracked files/dirs' '
	test_setup_reset keep &&
	(
		cd reset_keep &&

		mkdir foo.t &&
		echo precious >foo.t/file &&
		cp foo.t/file expect &&

		test_must_fail git reset --merge work 2>error &&
		test_cmp expect foo.t/file &&
		grep "Updating.*foo.t.*would lose untracked files" error
	)
'

test_setup_checkout_m () {
	test_create_repo checkout &&
	(
		cd checkout &&
		test_commit init &&

		test_write_lines file has some >filler &&
		git add filler &&
		git commit -m filler &&

		git branch stable &&

		git switch -c work &&
		echo stuff >notes.txt &&
		test_write_lines file has some words >filler &&
		git add notes.txt filler &&
		git commit -m filler &&

		git checkout stable
	)
}

test_expect_success 'checkout -m does not nuke untracked file' '
	test_setup_checkout_m &&
	(
		cd checkout &&

		# Tweak filler
		test_write_lines this file has some >filler &&
		# Make an untracked file, save its contents in "expect"
		echo precious >notes.txt &&
		cp notes.txt expect &&

		test_must_fail git checkout -m work &&
		test_cmp expect notes.txt
	)
'

test_setup_sequencing () {
	test_create_repo sequencing_$1 &&
	(
		cd sequencing_$1 &&
		test_commit init &&

		test_write_lines this file has some words >filler &&
		git add filler &&
		git commit -m filler &&

		mkdir -p foo/bar &&
		test_commit foo/bar/baz &&

		git branch simple &&
		git branch fooey &&

		git checkout fooey &&
		git rm foo/bar/baz.t &&
		echo stuff >>filler &&
		git add -u &&
		git commit -m "changes" &&

		git checkout simple &&
		echo items >>filler &&
		echo newstuff >>newfile &&
		git add filler newfile &&
		git commit -m another
	)
}

test_expect_success 'git rebase --abort and untracked files' '
	test_setup_sequencing rebase_abort_and_untracked &&
	(
		cd sequencing_rebase_abort_and_untracked &&
		git checkout fooey &&
		test_must_fail git rebase simple &&

		cat init.t &&
		git rm init.t &&
		echo precious >init.t &&
		cp init.t expect &&
		git status --porcelain &&
		test_must_fail git rebase --abort &&
		test_cmp expect init.t
	)
'

test_expect_success 'git rebase fast forwarding and untracked files' '
	test_setup_sequencing rebase_fast_forward_and_untracked &&
	(
		cd sequencing_rebase_fast_forward_and_untracked &&
		git checkout init &&
		echo precious >filler &&
		cp filler expect &&
		test_must_fail git rebase init simple &&
		test_cmp expect filler
	)
'

test_expect_failure 'git rebase --autostash and untracked files' '
	test_setup_sequencing rebase_autostash_and_untracked &&
	(
		cd sequencing_rebase_autostash_and_untracked &&
		git checkout simple &&
		git rm filler &&
		mkdir filler &&
		echo precious >filler/file &&
		cp filler/file expect &&
		git rebase --autostash init &&
		test_path_is_file filler/file
	)
'

test_expect_failure 'git stash and untracked files' '
	test_setup_sequencing stash_and_untracked_files &&
	(
		cd sequencing_stash_and_untracked_files &&
		git checkout simple &&
		git rm filler &&
		mkdir filler &&
		echo precious >filler/file &&
		cp filler/file expect &&
		git status --porcelain &&
		git stash push &&
		git status --porcelain &&
		test_path_is_file filler/file
	)
'

test_expect_success 'git am --abort and untracked dir vs. unmerged file' '
	test_setup_sequencing am_abort_and_untracked &&
	(
		cd sequencing_am_abort_and_untracked &&
		git format-patch -1 --stdout fooey >changes.mbox &&
		test_must_fail git am --3way changes.mbox &&

		# Delete the conflicted file; we will stage and commit it later
		rm filler &&

		# Put an unrelated untracked directory there
		mkdir filler &&
		echo foo >filler/file1 &&
		echo bar >filler/file2 &&

		test_must_fail git am --abort 2>errors &&
		test_path_is_dir filler &&
		grep "Updating .filler. would lose untracked files in it" errors
	)
'

test_expect_success 'git am --skip and untracked dir vs deleted file' '
	test_setup_sequencing am_skip_and_untracked &&
	(
		cd sequencing_am_skip_and_untracked &&
		git checkout fooey &&
		git format-patch -1 --stdout simple >changes.mbox &&
		test_must_fail git am --3way changes.mbox &&

		# Delete newfile
		rm newfile &&

		# Put an unrelated untracked directory there
		mkdir newfile &&
		echo foo >newfile/file1 &&
		echo bar >newfile/file2 &&

		# Change our mind about resolutions, just skip this patch
		test_must_fail git am --skip 2>errors &&
		test_path_is_dir newfile &&
		grep "Updating .newfile. would lose untracked files in it" errors
	)
'

test_done
