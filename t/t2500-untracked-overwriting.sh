#!/bin/sh

test_description='Test handling of overwriting untracked files'

. ./test-lib.sh

test_setup_reset () {
	but init reset_$1 &&
	(
		cd reset_$1 &&
		test_cummit init &&

		but branch stable &&
		but branch work &&

		but checkout work &&
		test_cummit foo &&

		but checkout stable
	)
}

test_expect_success 'reset --hard will nuke untracked files/dirs' '
	test_setup_reset hard &&
	(
		cd reset_hard &&
		but ls-tree -r stable &&
		but log --all --name-status --oneline &&
		but ls-tree -r work &&

		mkdir foo.t &&
		echo precious >foo.t/file &&
		echo foo >expect &&

		but reset --hard work &&

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

		test_must_fail but reset --merge work 2>error &&
		test_cmp expect foo.t/file &&
		grep "Updating .foo.t. would lose untracked files" error
	)
'

test_expect_success 'reset --keep will preserve untracked files/dirs' '
	test_setup_reset keep &&
	(
		cd reset_keep &&

		mkdir foo.t &&
		echo precious >foo.t/file &&
		cp foo.t/file expect &&

		test_must_fail but reset --merge work 2>error &&
		test_cmp expect foo.t/file &&
		grep "Updating.*foo.t.*would lose untracked files" error
	)
'

test_setup_checkout_m () {
	but init checkout &&
	(
		cd checkout &&
		test_cummit init &&

		test_write_lines file has some >filler &&
		but add filler &&
		but cummit -m filler &&

		but branch stable &&

		but switch -c work &&
		echo stuff >notes.txt &&
		test_write_lines file has some words >filler &&
		but add notes.txt filler &&
		but cummit -m filler &&

		but checkout stable
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

		test_must_fail but checkout -m work &&
		test_cmp expect notes.txt
	)
'

test_setup_sequencing () {
	but init sequencing_$1 &&
	(
		cd sequencing_$1 &&
		test_cummit init &&

		test_write_lines this file has some words >filler &&
		but add filler &&
		but cummit -m filler &&

		mkdir -p foo/bar &&
		test_cummit foo/bar/baz &&

		but branch simple &&
		but branch fooey &&

		but checkout fooey &&
		but rm foo/bar/baz.t &&
		echo stuff >>filler &&
		but add -u &&
		but cummit -m "changes" &&

		but checkout simple &&
		echo items >>filler &&
		echo newstuff >>newfile &&
		but add filler newfile &&
		but cummit -m another
	)
}

test_expect_success 'but rebase --abort and untracked files' '
	test_setup_sequencing rebase_abort_and_untracked &&
	(
		cd sequencing_rebase_abort_and_untracked &&
		but checkout fooey &&
		test_must_fail but rebase simple &&

		cat init.t &&
		but rm init.t &&
		echo precious >init.t &&
		cp init.t expect &&
		but status --porcelain &&
		test_must_fail but rebase --abort &&
		test_cmp expect init.t
	)
'

test_expect_success 'but rebase fast forwarding and untracked files' '
	test_setup_sequencing rebase_fast_forward_and_untracked &&
	(
		cd sequencing_rebase_fast_forward_and_untracked &&
		but checkout init &&
		echo precious >filler &&
		cp filler expect &&
		test_must_fail but rebase init simple &&
		test_cmp expect filler
	)
'

test_expect_failure 'but rebase --autostash and untracked files' '
	test_setup_sequencing rebase_autostash_and_untracked &&
	(
		cd sequencing_rebase_autostash_and_untracked &&
		but checkout simple &&
		but rm filler &&
		mkdir filler &&
		echo precious >filler/file &&
		cp filler/file expect &&
		but rebase --autostash init &&
		test_path_is_file filler/file
	)
'

test_expect_failure 'but stash and untracked files' '
	test_setup_sequencing stash_and_untracked_files &&
	(
		cd sequencing_stash_and_untracked_files &&
		but checkout simple &&
		but rm filler &&
		mkdir filler &&
		echo precious >filler/file &&
		cp filler/file expect &&
		but status --porcelain &&
		but stash push &&
		but status --porcelain &&
		test_path_is_file filler/file
	)
'

test_expect_success 'but am --abort and untracked dir vs. unmerged file' '
	test_setup_sequencing am_abort_and_untracked &&
	(
		cd sequencing_am_abort_and_untracked &&
		but format-patch -1 --stdout fooey >changes.mbox &&
		test_must_fail but am --3way changes.mbox &&

		# Delete the conflicted file; we will stage and cummit it later
		rm filler &&

		# Put an unrelated untracked directory there
		mkdir filler &&
		echo foo >filler/file1 &&
		echo bar >filler/file2 &&

		test_must_fail but am --abort 2>errors &&
		test_path_is_dir filler &&
		grep "Updating .filler. would lose untracked files in it" errors
	)
'

test_expect_success 'but am --skip and untracked dir vs deleted file' '
	test_setup_sequencing am_skip_and_untracked &&
	(
		cd sequencing_am_skip_and_untracked &&
		but checkout fooey &&
		but format-patch -1 --stdout simple >changes.mbox &&
		test_must_fail but am --3way changes.mbox &&

		# Delete newfile
		rm newfile &&

		# Put an unrelated untracked directory there
		mkdir newfile &&
		echo foo >newfile/file1 &&
		echo bar >newfile/file2 &&

		# Change our mind about resolutions, just skip this patch
		test_must_fail but am --skip 2>errors &&
		test_path_is_dir newfile &&
		grep "Updating .newfile. would lose untracked files in it" errors
	)
'

test_done
