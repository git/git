#!/bin/sh

test_description='Test handling of the current working directory becoming empty'

. ./test-lib.sh

test_expect_success setup '
	test_cummit init &&

	but branch fd_conflict &&

	mkdir -p foo/bar &&
	test_cummit foo/bar/baz &&

	but revert HEAD &&
	but tag reverted &&

	but checkout fd_conflict &&
	mkdir dirORfile &&
	test_cummit dirORfile/foo &&

	but rm -r dirORfile &&
	echo not-a-directory >dirORfile &&
	but add dirORfile &&
	but cummit -m dirORfile &&

	but switch -c df_conflict HEAD~1 &&
	test_cummit random_file &&

	but switch -c undo_fd_conflict fd_conflict &&
	but revert HEAD
'

test_incidental_dir_removal () {
	test_when_finished "but reset --hard" &&

	but checkout foo/bar/baz^{cummit} &&
	test_path_is_dir foo/bar &&

	(
		cd foo &&
		"$@" &&

		# Make sure foo still exists, and commands needing it work
		test-tool getcwd &&
		but status --porcelain
	) &&
	test_path_is_missing foo/bar/baz &&
	test_path_is_missing foo/bar &&

	test_path_is_dir foo
}

test_required_dir_removal () {
	but checkout df_conflict^{cummit} &&
	test_when_finished "but clean -fdx" &&

	(
		cd dirORfile &&

		# Ensure command refuses to run
		test_must_fail "$@" 2>../error &&
		grep "Refusing to remove.*current working directory" ../error &&

		# ...and that the index and working tree are left clean
		but diff --exit-code HEAD &&

		# Ensure that getcwd and but status do not error out (which
		# they might if the current working directory had been removed)
		test-tool getcwd &&
		but status --porcelain
	) &&

	test_path_is_dir dirORfile
}

test_expect_success 'checkout does not clean cwd incidentally' '
	test_incidental_dir_removal but checkout init
'

test_expect_success 'checkout fails if cwd needs to be removed' '
	test_required_dir_removal but checkout fd_conflict
'

test_expect_success 'reset --hard does not clean cwd incidentally' '
	test_incidental_dir_removal but reset --hard init
'

test_expect_success 'reset --hard fails if cwd needs to be removed' '
	test_required_dir_removal but reset --hard fd_conflict
'

test_expect_success 'merge does not clean cwd incidentally' '
	test_incidental_dir_removal but merge reverted
'

# This file uses some simple merges where
#   Base: 'dirORfile/' exists
#   Side1: random other file changed
#   Side2: 'dirORfile/' removed, 'dirORfile' added
# this should resolve cleanly, but merge-recursive throws merge conflicts
# because it's dumb.  Add a special test for checking merge-recursive (and
# merge-ort), then after this just hard require ort for all remaining tests.
#
test_expect_success 'merge fails if cwd needs to be removed; recursive friendly' '
	but checkout foo/bar/baz &&
	test_when_finished "but clean -fdx" &&

	mkdir dirORfile &&
	(
		cd dirORfile &&

		test_must_fail but merge fd_conflict 2>../error
	) &&

	test_path_is_dir dirORfile &&
	grep "Refusing to remove the current working directory" error
'

BUT_TEST_MERGE_ALGORITHM=ort

test_expect_success 'merge fails if cwd needs to be removed' '
	test_required_dir_removal but merge fd_conflict
'

test_expect_success 'cherry-pick does not clean cwd incidentally' '
	test_incidental_dir_removal but cherry-pick reverted
'

test_expect_success 'cherry-pick fails if cwd needs to be removed' '
	test_required_dir_removal but cherry-pick fd_conflict
'

test_expect_success 'rebase does not clean cwd incidentally' '
	test_incidental_dir_removal but rebase reverted
'

test_expect_success 'rebase fails if cwd needs to be removed' '
	test_required_dir_removal but rebase fd_conflict
'

test_expect_success 'revert does not clean cwd incidentally' '
	test_incidental_dir_removal but revert HEAD
'

test_expect_success 'revert fails if cwd needs to be removed' '
	test_required_dir_removal but revert undo_fd_conflict
'

test_expect_success 'rm does not clean cwd incidentally' '
	test_incidental_dir_removal but rm bar/baz.t
'

test_expect_success 'apply does not remove cwd incidentally' '
	but diff HEAD HEAD~1 >patch &&
	test_incidental_dir_removal but apply ../patch
'

test_incidental_untracked_dir_removal () {
	test_when_finished "but reset --hard" &&

	but checkout foo/bar/baz^{cummit} &&
	mkdir -p untracked &&
	mkdir empty
	>untracked/random &&

	(
		cd untracked &&
		"$@" &&

		# Make sure untracked still exists, and commands needing it work
		test-tool getcwd &&
		but status --porcelain
	) &&
	test_path_is_missing empty &&
	test_path_is_missing untracked/random &&

	test_path_is_dir untracked
}

test_expect_success 'clean does not remove cwd incidentally' '
	test_incidental_untracked_dir_removal \
		but -C .. clean -fd -e warnings . >warnings &&
	grep "Refusing to remove current working directory" warnings
'

test_expect_success 'stash does not remove cwd incidentally' '
	test_incidental_untracked_dir_removal \
		but stash --include-untracked
'

test_expect_success '`rm -rf dir` only removes a subset of dir' '
	test_when_finished "rm -rf a/" &&

	mkdir -p a/b/c &&
	>a/b/c/untracked &&
	>a/b/c/tracked &&
	but add a/b/c/tracked &&

	(
		cd a/b &&
		but rm -rf ../b
	) &&

	test_path_is_dir a/b &&
	test_path_is_missing a/b/c/tracked &&
	test_path_is_file a/b/c/untracked
'

test_expect_success '`rm -rf dir` even with only tracked files will remove something else' '
	test_when_finished "rm -rf a/" &&

	mkdir -p a/b/c &&
	>a/b/c/tracked &&
	but add a/b/c/tracked &&

	(
		cd a/b &&
		but rm -rf ../b
	) &&

	test_path_is_missing a/b/c/tracked &&
	test_path_is_missing a/b/c &&
	test_path_is_dir a/b
'

test_expect_success 'but version continues working from a deleted dir' '
	mkdir tmp &&
	(
		cd tmp &&
		rm -rf ../tmp &&
		but version
	)
'

test_submodule_removal () {
	path_status=$1 &&
	shift &&

	test_status=
	test "$path_status" = dir && test_status=test_must_fail

	test_when_finished "but reset --hard HEAD~1" &&
	test_when_finished "rm -rf .but/modules/my_submodule" &&

	but checkout foo/bar/baz &&

	but init my_submodule &&
	touch my_submodule/file &&
	but -C my_submodule add file &&
	but -C my_submodule cummit -m "initial cummit" &&
	but submodule add ./my_submodule &&
	but cummit -m "Add the submodule" &&

	(
		cd my_submodule &&
		$test_status "$@"
	) &&

	test_path_is_${path_status} my_submodule
}

test_expect_success 'rm -r with -C leaves submodule if cwd inside' '
	test_submodule_removal dir but -C .. rm -r my_submodule/
'

test_expect_success 'rm -r leaves submodule if cwd inside' '
	test_submodule_removal dir \
		but --but-dir=../.but --work-tree=.. rm -r ../my_submodule/
'

test_expect_success 'rm -rf removes submodule even if cwd inside' '
	test_submodule_removal missing \
		but --but-dir=../.but --work-tree=.. rm -rf ../my_submodule/
'

test_done
