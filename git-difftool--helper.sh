#!/bin/sh
# git-difftool--helper is a GIT_EXTERNAL_DIFF-compatible diff tool launcher.
# This script is typically launched by using the 'git difftool'
# convenience command.
#
# Copyright (c) 2009 David Aguilar

# Load common functions from git-mergetool--lib
TOOL_MODE=diff
. git-mergetool--lib

# difftool.prompt controls the default prompt/no-prompt behavior
# and is overridden with $GIT_DIFFTOOL*_PROMPT.
should_prompt () {
	prompt=$(git config --bool difftool.prompt || echo true)
	if test "$prompt" = true; then
		test -z "$GIT_DIFFTOOL_NO_PROMPT"
	else
		test -n "$GIT_DIFFTOOL_PROMPT"
	fi
}

# Sets up shell variables and runs a merge tool
launch_merge_tool () {
	# Merged is the filename as it appears in the work tree
	# Local is the contents of a/filename
	# Remote is the contents of b/filename
	# Custom merge tool commands might use $BASE so we provide it
	MERGED="$1"
	LOCAL="$2"
	REMOTE="$3"
	BASE="$1"

	# $LOCAL and $REMOTE are temporary files so prompt
	# the user with the real $MERGED name before launching $merge_tool.
	if should_prompt; then
		printf "\nViewing: '$MERGED'\n"
		printf "Hit return to launch '%s': " "$merge_tool"
		read ans
	fi

	# Run the appropriate merge tool command
	run_merge_tool "$merge_tool"
}

# Allow GIT_DIFF_TOOL and GIT_MERGE_TOOL to provide default values
test -n "$GIT_MERGE_TOOL" && merge_tool="$GIT_MERGE_TOOL"
test -n "$GIT_DIFF_TOOL" && merge_tool="$GIT_DIFF_TOOL"

if test -z "$merge_tool"; then
	merge_tool="$(get_merge_tool)" || exit
fi

# Launch the merge tool on each path provided by 'git diff'
while test $# -gt 6
do
	launch_merge_tool "$1" "$2" "$5"
	shift 7
done
