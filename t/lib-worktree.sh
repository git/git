# Helper functions for git worktree tests

# is_absolute_path - Determine if a given path is absolute.
#
# This function checks if the provided path is an absolute path.
# It handles Unix-like and Windows-style paths.
#
# Parameters:
# $1 - The path to check.
#
# Returns:
# 0 if the path is absolute.
# 1 if the path is relative.
is_absolute_path() {
	local path="$1"

	# Check for Unix-style absolute path (starts with /)
	case "$path" in
		/*) return 0 ;;
	esac

	# Check for Windows-style absolute path with backslashes (starts with drive letter followed by :\)
	case "$path" in
		[a-zA-Z]:\\*) return 0 ;;
	esac

	# Check for Windows-style absolute path with forward slashes (starts with drive letter followed by :/)
	case "$path" in
		[a-zA-Z]:/*) return 0 ;;
	esac

	return 1
}

# check_worktree_paths - Verify the format of the worktree paths.
#
# This function checks whether the paths specified in the worktree's
# configuration files (.git and gitdir files) are in the expected format,
# based on the provided configuration for relative or absolute paths.
#
# Parameters:
# $1 - Boolean value ("true" or "false") indicating whether relative paths
#      are expected. If "true", the function expects relative paths; otherwise,
#      it expects absolute paths.
# $2 - The path to the worktree directory.
#
# Functionality:
# - Reads the .git file in the specified worktree directory to extract the path
#   to the gitdir file.
# - Determines whether the extracted path is relative or absolute based
#   on the value of $1.
# - Checks if the gitdir file exists and if its contents match the expected path
#   format (relative or absolute).
# - Verify that the path in the gitdir file points back to the original worktree .git file.
# - Prints an error message and returns a non-zero exit code if any issues are
#   found, such as missing files or incorrect path formats.
# - Returns 0 if all checks pass and the paths are in the expected format.
check_worktree_paths() {
	local func_name="check_worktree_paths"
	local use_relative_paths="$1"
	local worktree_path="$2"
	if [ -d "$worktree_path" ]; then
		worktree_path="$(cd "$worktree_path" && pwd -P)"
	else
		echo "[$func_name] Error: Directory "$worktree_path" does not exist."
		return 1
	fi

	# Full path to the .git file in the worktree
	local git_file="$worktree_path/.git"

	# Check if the .git file exists
	if [ ! -f "$git_file" ]; then
		echo "[$func_name] Error: .git file not found in $worktree_path"
		return 1
	fi

	# Extract the path from the .git file
	local gitdir_path="$(sed 's/^gitdir: //' "$git_file")"

	# Check if the path is absolute or relative
	if [ "$use_relative_paths" = "true" ]; then
		# Ensure the path is relative
		if is_absolute_path "$gitdir_path"; then
			echo "[$func_name] Error: .git file contains an absolute path when a relative path was expected."
			echo "[$func_name] Path read from .git file: $gitdir_path"
			return 1
		fi
	else
		# Ensure the path is absolute
		if ! is_absolute_path "$gitdir_path"; then
			echo "[$func_name] Error: .git file contains a relative path when an absolute path was expected."
			echo "[$func_name] Path read from .git file: $gitdir_path"
			return 1
		fi
	fi

	# Resolve the gitdir path relative to worktree if necessary
	if ! is_absolute_path "$gitdir_path"; then
		gitdir_path="$(cd "$worktree_path"/"$gitdir_path" && pwd -P)"
	fi

	# Verify if gitdir_path is correct and the gitdir file exists
	local gitdir_file="$gitdir_path/gitdir"
	if [ ! -f "$gitdir_file" ]; then
		echo "[$func_name] Error: $gitdir_file not found"
		return 1
	fi

	# Read the stored path from the gitdir file
	local stored_path="$(cat "$gitdir_file")"

	if [ "$use_relative_paths" = "true" ]; then
		# Ensure the path is relative
		if is_absolute_path "$stored_path"; then
			echo "[$func_name] Error: $gitdir_file contains an absolute path when a relative path was expected."
			echo "[$func_name] Path read from gitdir file: $stored_path"
			return 1
		fi
	else
		# Ensure the path is absolute
		if ! is_absolute_path "$stored_path"; then
			echo "[$func_name] Error: $gitdir_file contains a relative path when an absolute path was expected."
			echo "[$func_name] Path read from gitdir file: $stored_path"
			return 1
		fi
	fi

	# Resolve the stored_path path to an absolute path
	if ! is_absolute_path "$stored_path"; then
		# Determine the repo dir, by removing the /.git/worktrees/<worktree_dir> or /worktrees/<worktree_dir> part
		local repo_dir="${gitdir_path%/*/*}"
		repo_dir="${repo_dir%/.git}"

		# If repo_dir is a relative path, resolve it against worktree_path
		if ! is_absolute_path "$repo_dir"; then
			repo_dir="$(cd "$worktree_path/$repo_dir" && pwd -P)"
		fi

		stored_path="$(cd "$(dirname "$repo_dir/$stored_path")" && pwd -P)/.git"
		if [ ! -f "$stored_path" ]; then
			echo "[$func_name] Error: File $stored_path does not exist."
			return 1
		fi
	fi

	# Verify that the stored_path points back to the original worktree .git file
	if [ "$stored_path" != "$git_file" ]; then
		echo "[$func_name]  Error: The gitdir file does not correctly reference the original .git file."
		echo "Expected: $git_file"
		echo "Found: $stored_path"
		return 1
	fi

	return 0
}
