#!/bin/sh

test_description='Test handling of the current working directory becoming empty'

. ./test-lib.sh

test_expect_success setup '
	test_commit init &&
	mkdir subdir &&
	test_commit subdir/file &&

	git branch fd_conflict &&

	mkdir -p foo/bar &&
	test_commit foo/bar/baz &&

	git revert HEAD &&
	git tag reverted &&

	git checkout fd_conflict &&
	git rm subdir/file.t &&
	echo not-a-directory >dirORfile &&
	git add dirORfile &&
	git commit -m dirORfile
'

test_expect_success 'checkout does not clean cwd incidentally' '
	git checkout foo/bar/baz &&
	test_path_is_dir foo/bar &&

	(
		cd foo &&
		git checkout init &&
		cd ..
	) &&
	test_path_is_missing foo/bar/baz &&
	test_path_is_missing foo/bar &&
	test_path_is_dir foo
'

test_expect_success 'checkout fails if cwd needs to be removed' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	mkdir dirORfile &&
	(
		cd dirORfile &&

		test_must_fail git checkout fd_conflict 2>../error &&
		grep "Refusing to remove the current working directory" ../error
	) &&

	test_path_is_dir dirORfile
'

test_expect_success 'reset --hard does not clean cwd incidentally' '
	git checkout foo/bar/baz &&
	test_path_is_dir foo/bar &&

	(
		cd foo &&
		git reset --hard init &&
		cd ..
	) &&
	test_path_is_missing foo/bar/baz &&
	test_path_is_missing foo/bar &&
	test_path_is_dir foo
'

test_expect_success 'reset --hard fails if cwd needs to be removed' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	mkdir dirORfile &&
	(
		cd dirORfile &&

		test_must_fail git reset --hard fd_conflict 2>../error &&
		grep "Refusing to remove.*the current working directory" ../error
	) &&

	test_path_is_dir dirORfile
'

test_expect_success 'merge does not remove cwd incidentally' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	(
		cd subdir &&
		git merge fd_conflict
	) &&

	test_path_is_missing subdir/file.t &&
	test_path_is_dir subdir
'

test_expect_success 'merge fails if cwd needs to be removed' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	mkdir dirORfile &&
	(
		cd dirORfile &&
		test_must_fail git merge fd_conflict 2>../error &&
		grep "Refusing to remove the current working directory" ../error
	) &&

	test_path_is_dir dirORfile
'

test_expect_success 'cherry-pick does not remove cwd incidentally' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	(
		cd subdir &&
		git cherry-pick fd_conflict
	) &&

	test_path_is_missing subdir/file.t &&
	test_path_is_dir subdir
'

test_expect_success 'cherry-pick fails if cwd needs to be removed' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	mkdir dirORfile &&
	(
		cd dirORfile &&
		test_must_fail git cherry-pick fd_conflict 2>../error &&
		grep "Refusing to remove the current working directory" ../error
	) &&

	test_path_is_dir dirORfile
'

test_expect_success 'rebase does not remove cwd incidentally' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	(
		cd subdir &&
		git rebase foo/bar/baz fd_conflict
	) &&

	test_path_is_missing subdir/file.t &&
	test_path_is_dir subdir
'

test_expect_success 'rebase fails if cwd needs to be removed' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	mkdir dirORfile &&
	(
		cd dirORfile &&
		test_must_fail git rebase foo/bar/baz fd_conflict 2>../error &&
		grep "Refusing to remove the current working directory" ../error
	) &&

	test_path_is_dir dirORfile
'

test_expect_success 'revert does not remove cwd incidentally' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	(
		cd subdir &&
		git revert subdir/file
	) &&

	test_path_is_missing subdir/file.t &&
	test_path_is_dir subdir
'

test_expect_success 'revert fails if cwd needs to be removed' '
	git checkout fd_conflict &&
	git revert HEAD &&
	test_when_finished "git clean -fdx" &&

	mkdir dirORfile &&
	(
		cd dirORfile &&
		test_must_fail git revert HEAD 2>../error &&
		grep "Refusing to remove the current working directory" ../error
	) &&

	test_path_is_dir dirORfile
'

test_expect_success 'rm does not remove cwd incidentally' '
	test_when_finished "git reset --hard" &&
	git checkout foo/bar/baz &&

	(
		cd foo &&
		git rm bar/baz.t
	) &&

	test_path_is_missing foo/bar/baz &&
	test_path_is_missing foo/bar &&
	test_path_is_dir foo
'

test_expect_success 'apply does not remove cwd incidentally' '
	test_when_finished "git reset --hard" &&
	git checkout foo/bar/baz &&

	(
		cd subdir &&
		git diff subdir/file init | git apply
	) &&

	test_path_is_missing subdir/file.t &&
	test_path_is_dir subdir
'

test_expect_success 'clean does not remove cwd incidentally' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	mkdir empty &&
	mkdir untracked &&
	>untracked/random &&
	(
		cd untracked &&
		git clean -fd -e warnings :/ >../warnings &&
		grep "Refusing to remove current working directory" ../warnings
	) &&

	test_path_is_missing empty &&
	test_path_is_missing untracked/random &&
	test_path_is_dir untracked
'

test_expect_success 'stash does not remove cwd incidentally' '
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	mkdir untracked &&
	>untracked/random &&
	(
		cd untracked &&
		git stash --include-untracked &&
		git status
	) &&

	test_path_is_missing untracked/random &&
	test_path_is_dir untracked
'

test_done
