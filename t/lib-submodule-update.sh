# Create a submodule layout used for all tests below.
#
# The following use cases are covered:
# - New submodule (no_submodule => add_sub1)
# - Removed submodule (add_sub1 => remove_sub1)
# - Updated submodule (add_sub1 => modify_sub1)
# - Submodule updated to invalid commit (add_sub1 => invalid_sub1)
# - Submodule updated from invalid commit (invalid_sub1 => valid_sub1)
# - Submodule replaced by tracked files in directory (add_sub1 =>
#   replace_sub1_with_directory)
# - Directory containing tracked files replaced by submodule
#   (replace_sub1_with_directory => replace_directory_with_sub1)
# - Submodule replaced by tracked file with the same name (add_sub1 =>
#   replace_sub1_with_file)
# - Tracked file replaced by submodule (replace_sub1_with_file =>
#   replace_file_with_sub1)
#
#                   --O-----O
#                  /  ^     replace_directory_with_sub1
#                 /   replace_sub1_with_directory
#                /----O
#               /     ^
#              /      modify_sub1
#      O------O-------O
#      ^      ^\      ^
#      |      | \     remove_sub1
#      |      |  -----O-----O
#      |      |   \   ^     replace_file_with_sub1
#      |      |    \  replace_sub1_with_file
#      |   add_sub1 --O-----O
# no_submodule        ^     valid_sub1
#                     invalid_sub1
#
create_lib_submodule_repo () {
	git init submodule_update_repo &&
	(
		cd submodule_update_repo &&
		echo "expect" >>.gitignore &&
		echo "actual" >>.gitignore &&
		echo "x" >file1 &&
		echo "y" >file2 &&
		git add .gitignore file1 file2 &&
		git commit -m "Base" &&
		git branch "no_submodule" &&

		git checkout -b "add_sub1" &&
		git submodule add ./. sub1 &&
		git config -f .gitmodules submodule.sub1.ignore all &&
		git config submodule.sub1.ignore all &&
		git add .gitmodules &&
		git commit -m "Add sub1" &&
		git checkout -b remove_sub1 &&
		git revert HEAD &&

		git checkout -b "modify_sub1" "add_sub1" &&
		git submodule update &&
		(
			cd sub1 &&
			git fetch &&
			git checkout -b "modifications" &&
			echo "z" >file2 &&
			echo "x" >file3 &&
			git add file2 file3 &&
			git commit -m "modified file2 and added file3" &&
			git push origin modifications
		) &&
		git add sub1 &&
		git commit -m "Modify sub1" &&

		git checkout -b "replace_sub1_with_directory" "add_sub1" &&
		git submodule update &&
		(
			cd sub1 &&
			git checkout modifications
		) &&
		git rm --cached sub1 &&
		rm sub1/.git* &&
		git config -f .gitmodules --remove-section "submodule.sub1" &&
		git add .gitmodules sub1/* &&
		git commit -m "Replace sub1 with directory" &&
		git checkout -b replace_directory_with_sub1 &&
		git revert HEAD &&

		git checkout -b "replace_sub1_with_file" "add_sub1" &&
		git rm sub1 &&
		echo "content" >sub1 &&
		git add sub1 &&
		git commit -m "Replace sub1 with file" &&
		git checkout -b replace_file_with_sub1 &&
		git revert HEAD &&

		git checkout -b "invalid_sub1" "add_sub1" &&
		git update-index --cacheinfo 160000 0123456789012345678901234567890123456789 sub1 &&
		git commit -m "Invalid sub1 commit" &&
		git checkout -b valid_sub1 &&
		git revert HEAD &&
		git checkout master
	)
}

# Helper function to replace gitfile with .git directory
replace_gitfile_with_git_dir () {
	(
		cd "$1" &&
		git_dir="$(git rev-parse --git-dir)" &&
		rm -f .git &&
		cp -R "$git_dir" .git &&
		GIT_WORK_TREE=. git config --unset core.worktree
	)
}

# Test that the .git directory in the submodule is unchanged (except for the
# core.worktree setting, which appears only in $GIT_DIR/modules/$1/config).
# Call this function before test_submodule_content as the latter might
# write the index file leading to false positive index differences.
#
# Note that this only supports submodules at the root level of the
# superproject, with the default name, i.e. same as its path.
test_git_directory_is_unchanged () {
	(
		cd ".git/modules/$1" &&
		# does core.worktree point at the right place?
		test "$(git config core.worktree)" = "../../../$1" &&
		# remove it temporarily before comparing, as
		# "$1/.git/config" lacks it...
		git config --unset core.worktree
	) &&
	diff -r ".git/modules/$1" "$1/.git" &&
	(
		# ... and then restore.
		cd ".git/modules/$1" &&
		git config core.worktree "../../../$1"
	)
}

# Helper function to be executed at the start of every test below, it sets up
# the submodule repo if it doesn't exist and configures the most problematic
# settings for diff.ignoreSubmodules.
prolog () {
	(test -d submodule_update_repo || create_lib_submodule_repo) &&
	test_config_global diff.ignoreSubmodules all &&
	test_config diff.ignoreSubmodules all
}

# Helper function to bring work tree back into the state given by the
# commit. This includes trying to populate sub1 accordingly if it exists and
# should be updated to an existing commit.
reset_work_tree_to () {
	rm -rf submodule_update &&
	git clone submodule_update_repo submodule_update &&
	(
		cd submodule_update &&
		rm -rf sub1 &&
		git checkout -f "$1" &&
		git status -u -s >actual &&
		test_must_be_empty actual &&
		sha1=$(git rev-parse --revs-only HEAD:sub1) &&
		if test -n "$sha1" &&
		   test $(cd "sub1" && git rev-parse --verify "$sha1^{commit}")
		then
			git submodule update --init --recursive "sub1"
		fi
	)
}

# Test that the superproject contains the content according to commit "$1"
# (the work tree must match the index for everything but submodules but the
# index must exactly match the given commit including any submodule SHA-1s).
test_superproject_content () {
	git diff-index --cached "$1" >actual &&
	test_must_be_empty actual &&
	git diff-files --ignore-submodules >actual &&
	test_must_be_empty actual
}

# Test that the given submodule at path "$1" contains the content according
# to the submodule commit recorded in the superproject's commit "$2"
test_submodule_content () {
	if test $# != 2
	then
		echo "test_submodule_content needs two arguments"
		return 1
	fi &&
	submodule="$1" &&
	commit="$2" &&
	test -d "$submodule"/ &&
	if ! test -f "$submodule"/.git && ! test -d "$submodule"/.git
	then
		echo "Submodule $submodule is not populated"
		return 1
	fi &&
	sha1=$(git rev-parse --verify "$commit:$submodule") &&
	if test -z "$sha1"
	then
		echo "Couldn't retrieve SHA-1 of $submodule for $commit"
		return 1
	fi &&
	(
		cd "$submodule" &&
		git status -u -s >actual &&
		test_must_be_empty actual &&
		git diff "$sha1" >actual &&
		test_must_be_empty actual
	)
}

# Test that the following transitions are correctly handled:
# - Updated submodule
# - New submodule
# - Removed submodule
# - Directory containing tracked files replaced by submodule
# - Submodule replaced by tracked files in directory
# - Submodule replaced by tracked file with the same name
# - tracked file replaced by submodule
#
# The default is that submodule contents aren't changed until "git submodule
# update" is run. And even then that command doesn't delete the work tree of
# a removed submodule.
#
# Removing a submodule containing a .git directory must fail even when forced
# to protect the history!
#

# Test that submodule contents are currently not updated when switching
# between commits that change a submodule.
test_submodule_switch () {
	command="$1"
	######################### Appearing submodule #########################
	# Switching to a commit letting a submodule appear creates empty dir ...
	if test "$KNOWN_FAILURE_STASH_DOES_IGNORE_SUBMODULE_CHANGES" = 1
	then
		# Restoring stash fails to restore submodule index entry
		RESULT="failure"
	else
		RESULT="success"
	fi
	test_expect_$RESULT "$command: added submodule creates empty directory" '
		prolog &&
		reset_work_tree_to no_submodule &&
		(
			cd submodule_update &&
			git branch -t add_sub1 origin/add_sub1 &&
			$command add_sub1 &&
			test_superproject_content origin/add_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... and doesn't care if it already exists ...
	test_expect_$RESULT "$command: added submodule leaves existing empty directory alone" '
		prolog &&
		reset_work_tree_to no_submodule &&
		(
			cd submodule_update &&
			mkdir sub1 &&
			git branch -t add_sub1 origin/add_sub1 &&
			$command add_sub1 &&
			test_superproject_content origin/add_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... unless there is an untracked file in its place.
	test_expect_success "$command: added submodule doesn't remove untracked unignored file with same name" '
		prolog &&
		reset_work_tree_to no_submodule &&
		(
			cd submodule_update &&
			git branch -t add_sub1 origin/add_sub1 &&
			>sub1 &&
			test_must_fail $command add_sub1 &&
			test_superproject_content origin/no_submodule &&
			test_must_be_empty sub1
		)
	'
	# Replacing a tracked file with a submodule produces an empty
	# directory ...
	test_expect_$RESULT "$command: replace tracked file with submodule creates empty directory" '
		prolog &&
		reset_work_tree_to replace_sub1_with_file &&
		(
			cd submodule_update &&
			git branch -t replace_file_with_sub1 origin/replace_file_with_sub1 &&
			$command replace_file_with_sub1 &&
			test_superproject_content origin/replace_file_with_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/replace_file_with_sub1
		)
	'
	# ... as does removing a directory with tracked files with a
	# submodule.
	if test "$KNOWN_FAILURE_NOFF_MERGE_DOESNT_CREATE_EMPTY_SUBMODULE_DIR" = 1
	then
		# Non fast-forward merges fail with "Directory sub1 doesn't
		# exist. sub1" because the empty submodule directory is not
		# created
		RESULT="failure"
	else
		RESULT="success"
	fi
	test_expect_$RESULT "$command: replace directory with submodule" '
		prolog &&
		reset_work_tree_to replace_sub1_with_directory &&
		(
			cd submodule_update &&
			git branch -t replace_directory_with_sub1 origin/replace_directory_with_sub1 &&
			$command replace_directory_with_sub1 &&
			test_superproject_content origin/replace_directory_with_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/replace_directory_with_sub1
		)
	'

	######################## Disappearing submodule #######################
	# Removing a submodule doesn't remove its work tree ...
	if test "$KNOWN_FAILURE_STASH_DOES_IGNORE_SUBMODULE_CHANGES" = 1
	then
		RESULT="failure"
	else
		RESULT="success"
	fi
	test_expect_$RESULT "$command: removed submodule leaves submodule directory and its contents in place" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t remove_sub1 origin/remove_sub1 &&
			$command remove_sub1 &&
			test_superproject_content origin/remove_sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... especially when it contains a .git directory.
	test_expect_$RESULT "$command: removed submodule leaves submodule containing a .git directory alone" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t remove_sub1 origin/remove_sub1 &&
			replace_gitfile_with_git_dir sub1 &&
			$command remove_sub1 &&
			test_superproject_content origin/remove_sub1 &&
			test_git_directory_is_unchanged sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# Replacing a submodule with files in a directory must fail as the
	# submodule work tree isn't removed ...
	if test "$KNOWN_FAILURE_NOFF_MERGE_ATTEMPTS_TO_MERGE_REMOVED_SUBMODULE_FILES" = 1
	then
		# Non fast-forward merges attempt to merge the former
		# submodule files with the newly checked out ones in the
		# directory of the same name while it shouldn't.
		RESULT="failure"
	else
		RESULT="success"
	fi
	test_expect_$RESULT "$command: replace submodule with a directory must fail" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t replace_sub1_with_directory origin/replace_sub1_with_directory &&
			test_must_fail $command replace_sub1_with_directory &&
			test_superproject_content origin/add_sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... especially when it contains a .git directory.
	test_expect_$RESULT "$command: replace submodule containing a .git directory with a directory must fail" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t replace_sub1_with_directory origin/replace_sub1_with_directory &&
			replace_gitfile_with_git_dir sub1 &&
			test_must_fail $command replace_sub1_with_directory &&
			test_superproject_content origin/add_sub1 &&
			test_git_directory_is_unchanged sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# Replacing it with a file must fail as it could throw away any local
	# work tree changes ...
	test_expect_failure "$command: replace submodule with a file must fail" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t replace_sub1_with_file origin/replace_sub1_with_file &&
			test_must_fail $command replace_sub1_with_file &&
			test_superproject_content origin/add_sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... or even destroy unpushed parts of submodule history if that
	# still uses a .git directory.
	test_expect_failure "$command: replace submodule containing a .git directory with a file must fail" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t replace_sub1_with_file origin/replace_sub1_with_file &&
			replace_gitfile_with_git_dir sub1 &&
			test_must_fail $command replace_sub1_with_file &&
			test_superproject_content origin/add_sub1 &&
			test_git_directory_is_unchanged sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'

	########################## Modified submodule #########################
	# Updating a submodule sha1 doesn't update the submodule's work tree
	if test "$KNOWN_FAILURE_CHERRY_PICK_SEES_EMPTY_COMMIT" = 1
	then
		# When cherry picking a SHA-1 update for an ignored submodule
		# the commit incorrectly fails with "The previous cherry-pick
		# is now empty, possibly due to conflict resolution."
		RESULT="failure"
	else
		RESULT="success"
	fi
	test_expect_$RESULT "$command: modified submodule does not update submodule work tree" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t modify_sub1 origin/modify_sub1 &&
			$command modify_sub1 &&
			test_superproject_content origin/modify_sub1 &&
			test_submodule_content sub1 origin/add_sub1 &&
			git submodule update &&
			test_submodule_content sub1 origin/modify_sub1
		)
	'

	# Updating a submodule to an invalid sha1 doesn't update the
	# submodule's work tree, subsequent update will fail
	test_expect_$RESULT "$command: modified submodule does not update submodule work tree to invalid commit" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t invalid_sub1 origin/invalid_sub1 &&
			$command invalid_sub1 &&
			test_superproject_content origin/invalid_sub1 &&
			test_submodule_content sub1 origin/add_sub1 &&
			test_must_fail git submodule update &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# Updating a submodule from an invalid sha1 doesn't update the
	# submodule's work tree, subsequent update will succeed
	test_expect_$RESULT "$command: modified submodule does not update submodule work tree from invalid commit" '
		prolog &&
		reset_work_tree_to invalid_sub1 &&
		(
			cd submodule_update &&
			git branch -t valid_sub1 origin/valid_sub1 &&
			$command valid_sub1 &&
			test_superproject_content origin/valid_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/valid_sub1
		)
	'
}

# Test that submodule contents are currently not updated when switching
# between commits that change a submodule, but throwing away local changes in
# the superproject is allowed.
test_submodule_forced_switch () {
	command="$1"
	######################### Appearing submodule #########################
	# Switching to a commit letting a submodule appear creates empty dir ...
	test_expect_success "$command: added submodule creates empty directory" '
		prolog &&
		reset_work_tree_to no_submodule &&
		(
			cd submodule_update &&
			git branch -t add_sub1 origin/add_sub1 &&
			$command add_sub1 &&
			test_superproject_content origin/add_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... and doesn't care if it already exists ...
	test_expect_success "$command: added submodule leaves existing empty directory alone" '
		prolog &&
		reset_work_tree_to no_submodule &&
		(
			cd submodule_update &&
			git branch -t add_sub1 origin/add_sub1 &&
			mkdir sub1 &&
			$command add_sub1 &&
			test_superproject_content origin/add_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... unless there is an untracked file in its place.
	test_expect_success "$command: added submodule does remove untracked unignored file with same name when forced" '
		prolog &&
		reset_work_tree_to no_submodule &&
		(
			cd submodule_update &&
			git branch -t add_sub1 origin/add_sub1 &&
			>sub1 &&
			$command add_sub1 &&
			test_superproject_content origin/add_sub1 &&
			test_dir_is_empty sub1
		)
	'
	# Replacing a tracked file with a submodule produces an empty
	# directory ...
	test_expect_success "$command: replace tracked file with submodule creates empty directory" '
		prolog &&
		reset_work_tree_to replace_sub1_with_file &&
		(
			cd submodule_update &&
			git branch -t replace_file_with_sub1 origin/replace_file_with_sub1 &&
			$command replace_file_with_sub1 &&
			test_superproject_content origin/replace_file_with_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/replace_file_with_sub1
		)
	'
	# ... as does removing a directory with tracked files with a
	# submodule.
	test_expect_success "$command: replace directory with submodule" '
		prolog &&
		reset_work_tree_to replace_sub1_with_directory &&
		(
			cd submodule_update &&
			git branch -t replace_directory_with_sub1 origin/replace_directory_with_sub1 &&
			$command replace_directory_with_sub1 &&
			test_superproject_content origin/replace_directory_with_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/replace_directory_with_sub1
		)
	'

	######################## Disappearing submodule #######################
	# Removing a submodule doesn't remove its work tree ...
	test_expect_success "$command: removed submodule leaves submodule directory and its contents in place" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t remove_sub1 origin/remove_sub1 &&
			$command remove_sub1 &&
			test_superproject_content origin/remove_sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... especially when it contains a .git directory.
	test_expect_success "$command: removed submodule leaves submodule containing a .git directory alone" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t remove_sub1 origin/remove_sub1 &&
			replace_gitfile_with_git_dir sub1 &&
			$command remove_sub1 &&
			test_superproject_content origin/remove_sub1 &&
			test_git_directory_is_unchanged sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# Replacing a submodule with files in a directory must fail as the
	# submodule work tree isn't removed ...
	test_expect_failure "$command: replace submodule with a directory must fail" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t replace_sub1_with_directory origin/replace_sub1_with_directory &&
			test_must_fail $command replace_sub1_with_directory &&
			test_superproject_content origin/add_sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... especially when it contains a .git directory.
	test_expect_failure "$command: replace submodule containing a .git directory with a directory must fail" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t replace_sub1_with_directory origin/replace_sub1_with_directory &&
			replace_gitfile_with_git_dir sub1 &&
			test_must_fail $command replace_sub1_with_directory &&
			test_superproject_content origin/add_sub1 &&
			test_git_directory_is_unchanged sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# Replacing it with a file must fail as it could throw away any local
	# work tree changes ...
	test_expect_failure "$command: replace submodule with a file must fail" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t replace_sub1_with_file origin/replace_sub1_with_file &&
			test_must_fail $command replace_sub1_with_file &&
			test_superproject_content origin/add_sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# ... or even destroy unpushed parts of submodule history if that
	# still uses a .git directory.
	test_expect_failure "$command: replace submodule containing a .git directory with a file must fail" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t replace_sub1_with_file origin/replace_sub1_with_file &&
			replace_gitfile_with_git_dir sub1 &&
			test_must_fail $command replace_sub1_with_file &&
			test_superproject_content origin/add_sub1 &&
			test_git_directory_is_unchanged sub1 &&
			test_submodule_content sub1 origin/add_sub1
		)
	'

	########################## Modified submodule #########################
	# Updating a submodule sha1 doesn't update the submodule's work tree
	test_expect_success "$command: modified submodule does not update submodule work tree" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t modify_sub1 origin/modify_sub1 &&
			$command modify_sub1 &&
			test_superproject_content origin/modify_sub1 &&
			test_submodule_content sub1 origin/add_sub1 &&
			git submodule update &&
			test_submodule_content sub1 origin/modify_sub1
		)
	'
	# Updating a submodule to an invalid sha1 doesn't update the
	# submodule's work tree, subsequent update will fail
	test_expect_success "$command: modified submodule does not update submodule work tree to invalid commit" '
		prolog &&
		reset_work_tree_to add_sub1 &&
		(
			cd submodule_update &&
			git branch -t invalid_sub1 origin/invalid_sub1 &&
			$command invalid_sub1 &&
			test_superproject_content origin/invalid_sub1 &&
			test_submodule_content sub1 origin/add_sub1 &&
			test_must_fail git submodule update &&
			test_submodule_content sub1 origin/add_sub1
		)
	'
	# Updating a submodule from an invalid sha1 doesn't update the
	# submodule's work tree, subsequent update will succeed
	test_expect_success "$command: modified submodule does not update submodule work tree from invalid commit" '
		prolog &&
		reset_work_tree_to invalid_sub1 &&
		(
			cd submodule_update &&
			git branch -t valid_sub1 origin/valid_sub1 &&
			$command valid_sub1 &&
			test_superproject_content origin/valid_sub1 &&
			test_dir_is_empty sub1 &&
			git submodule update --init --recursive &&
			test_submodule_content sub1 origin/valid_sub1
		)
	'
}
