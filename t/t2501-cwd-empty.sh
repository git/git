#!/bin/sh

test_description='Test handling of the current working directory becoming empty'

. ./test-lib.sh

test_expect_success setup '
	test_commit init &&

	git branch fd_conflict &&

	mkdir -p foo/bar &&
	test_commit foo/bar/baz &&

	git revert HEAD &&
	git tag reverted &&

	git checkout fd_conflict &&
	mkdir dirORfile &&
	test_commit dirORfile/foo &&

	git rm -r dirORfile &&
	echo not-a-directory >dirORfile &&
	git add dirORfile &&
	git commit -m dirORfile &&

	git switch -c df_conflict HEAD~1 &&
	test_commit random_file &&

	git switch -c undo_fd_conflict fd_conflict &&
	git revert HEAD
'

test_incidental_dir_removal () {
	works=$1 &&
	shift &&

	test_when_finished "git reset --hard" &&

	git checkout foo/bar/baz^{commit} &&
	test_path_is_dir foo/bar &&

	(
		cd foo &&
		"$@" &&

		# Although we want pwd & git status to pass, test for existing
		# rather than desired behavior.
		if test "$works" = "success"
		then
			test-tool getcwd &&
			git status --porcelain
		else
			! test-tool getcwd &&
			test_might_fail git status --porcelain
		fi
	) &&
	test_path_is_missing foo/bar/baz &&
	test_path_is_missing foo/bar &&

	# Although we want dir to be present, test for existing rather
	# than desired behavior.
	if test "$works" = "success"
	then
		test_path_is_dir foo
	else
		test_path_is_missing foo
	fi
}

test_required_dir_removal () {
	works=$1 &&
	shift &&

	git checkout df_conflict^{commit} &&
	test_when_finished "git clean -fdx" &&

	(
		cd dirORfile &&

		# We'd like for the command to fail (much as it would if there
		# was an untracked file there), and for the index and worktree
		# to be left clean with pwd and git status working afterwards.
		# But test for existing rather than desired behavior.
		if test "$works" = "success"
		then
			test_must_fail "$@" 2>../error &&
			grep "Refusing to remove.*current working directory" ../error &&

			git diff --exit-code HEAD &&

			test-tool getcwd &&
			git status --porcelain
		else
			"$@" &&
			! test-tool getcwd &&
			test_might_fail git status --porcelain
		fi
	) &&

	# Although we want dirORfile to be present, test for existing rather
	# than desired behavior.
	if test "$works" = "success"
	then
		test_path_is_dir dirORfile
	else
		test_path_is_file dirORfile
	fi
}

test_expect_success 'checkout does not clean cwd incidentally' '
	test_incidental_dir_removal failure git checkout init
'

test_expect_success 'checkout fails if cwd needs to be removed' '
	test_required_dir_removal success git checkout fd_conflict
'

test_expect_success 'reset --hard does not clean cwd incidentally' '
	test_incidental_dir_removal failure git reset --hard init
'

test_expect_success 'reset --hard fails if cwd needs to be removed' '
	test_required_dir_removal success git reset --hard fd_conflict
'

test_expect_success 'merge does not clean cwd incidentally' '
	test_incidental_dir_removal failure git merge reverted
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
	git checkout foo/bar/baz &&
	test_when_finished "git clean -fdx" &&

	mkdir dirORfile &&
	(
		cd dirORfile &&

		test_must_fail git merge fd_conflict 2>../error
	) &&

	test_path_is_dir dirORfile &&
	grep "Refusing to remove the current working directory" error
'

GIT_TEST_MERGE_ALGORITHM=ort

test_expect_success 'merge fails if cwd needs to be removed' '
	test_required_dir_removal success git merge fd_conflict
'

test_expect_success 'cherry-pick does not clean cwd incidentally' '
	test_incidental_dir_removal failure git cherry-pick reverted
'

test_expect_success 'cherry-pick fails if cwd needs to be removed' '
	test_required_dir_removal success git cherry-pick fd_conflict
'

test_expect_success 'rebase does not clean cwd incidentally' '
	test_incidental_dir_removal failure git rebase reverted
'

test_expect_success 'rebase fails if cwd needs to be removed' '
	test_required_dir_removal failure git rebase fd_conflict
'

test_expect_success 'revert does not clean cwd incidentally' '
	test_incidental_dir_removal failure git revert HEAD
'

test_expect_success 'revert fails if cwd needs to be removed' '
	test_required_dir_removal success git revert undo_fd_conflict
'

test_expect_success 'rm does not clean cwd incidentally' '
	test_incidental_dir_removal failure git rm bar/baz.t
'

test_expect_success 'apply does not remove cwd incidentally' '
	git diff HEAD HEAD~1 >patch &&
	test_incidental_dir_removal failure git apply ../patch
'

test_incidental_untracked_dir_removal () {
	works=$1 &&
	shift &&

	test_when_finished "git reset --hard" &&

	git checkout foo/bar/baz^{commit} &&
	mkdir -p untracked &&
	mkdir empty
	>untracked/random &&

	(
		cd untracked &&
		"$@" &&

		# Although we want pwd & git status to pass, test for existing
		# rather than desired behavior.
		if test "$works" = "success"
		then
			test-tool getcwd &&
			git status --porcelain
		else
			! test-tool getcwd &&
			test_might_fail git status --porcelain
		fi
	) &&
	test_path_is_missing empty &&
	test_path_is_missing untracked/random &&

	# Although we want dir to be present, test for existing rather
	# than desired behavior.
	if test "$works" = "success"
	then
		test_path_is_dir untracked
	else
		test_path_is_missing untracked
	fi
}

test_expect_success 'clean does not remove cwd incidentally' '
	test_incidental_untracked_dir_removal failure \
		git -C .. clean -fd -e warnings . >warnings
'

test_expect_success 'stash does not remove cwd incidentally' '
	test_incidental_untracked_dir_removal failure \
		git stash --include-untracked
'

test_expect_success '`rm -rf dir` only removes a subset of dir' '
	test_when_finished "rm -rf a/" &&

	mkdir -p a/b/c &&
	>a/b/c/untracked &&
	>a/b/c/tracked &&
	git add a/b/c/tracked &&

	(
		cd a/b &&
		git rm -rf ../b
	) &&

	test_path_is_dir a/b &&
	test_path_is_missing a/b/c/tracked &&
	test_path_is_file a/b/c/untracked
'

test_expect_success '`rm -rf dir` even with only tracked files will remove something else' '
	test_when_finished "rm -rf a/" &&

	mkdir -p a/b/c &&
	>a/b/c/tracked &&
	git add a/b/c/tracked &&

	(
		cd a/b &&
		git rm -rf ../b
	) &&

	test_path_is_missing a/b/c/tracked &&
	## We would prefer if a/b was still present, though empty, since it
	## was the current working directory
	#test_path_is_dir a/b
	## But the current behavior is that it not only deletes the directory
	## a/b as requested, but also goes and deletes a
	test_path_is_missing a
'

test_expect_success 'git version continues working from a deleted dir' '
	mkdir tmp &&
	(
		cd tmp &&
		rm -rf ../tmp &&
		git version
	)
'

test_submodule_removal () {
	path_status=$1 &&
	shift &&

	test_status=
	test "$path_status" = dir && test_status=test_must_fail

	# Actually, while path_status=dir && test_status=test_must_fail
	# reflect our desired behavior, current behavior is:
	path_status=missing
	test_status=

	test_when_finished "git reset --hard HEAD~1" &&
	test_when_finished "rm -rf .git/modules/my_submodule" &&

	git checkout foo/bar/baz &&

	git init my_submodule &&
	touch my_submodule/file &&
	git -C my_submodule add file &&
	git -C my_submodule commit -m "initial commit" &&
	git submodule add ./my_submodule &&
	git commit -m "Add the submodule" &&

	(
		cd my_submodule &&
		$test_status "$@"
	) &&

	test_path_is_${path_status} my_submodule
}

test_expect_success 'rm -r with -C leaves submodule if cwd inside' '
	test_submodule_removal dir git -C .. rm -r my_submodule/
'

test_expect_success 'rm -r leaves submodule if cwd inside' '
	test_submodule_removal dir \
		git --git-dir=../.git --work-tree=.. rm -r ../my_submodule/
'

test_expect_success 'rm -rf removes submodule even if cwd inside' '
	test_submodule_removal missing \
		git --git-dir=../.git --work-tree=.. rm -rf ../my_submodule/
'

test_done
