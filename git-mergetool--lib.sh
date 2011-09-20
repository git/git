#!/bin/sh
# git-mergetool--lib is a library for common merge tool functions
diff_mode() {
	test "$TOOL_MODE" = diff
}

merge_mode() {
	test "$TOOL_MODE" = merge
}

translate_merge_tool_path () {
	echo "$1"
}

check_unchanged () {
	if test "$MERGED" -nt "$BACKUP"
	then
		status=0
	else
		while true
		do
			echo "$MERGED seems unchanged."
			printf "Was the merge successful? [y/n] "
			read answer || return 1
			case "$answer" in
			y*|Y*) status=0; break ;;
			n*|N*) status=1; break ;;
			esac
		done
	fi
}

valid_tool_config () {
	if test -n "$(get_merge_tool_cmd "$1")"
	then
		return 0
	else
		return 1
	fi
}

valid_tool () {
	setup_tool "$1" || valid_tool_config "$1"
}

setup_tool () {
	case "$1" in
	vim*|gvim*)
		tool=vim
		;;
	*)
		tool="$1"
		;;
	esac
	mergetools="$(git --exec-path)/mergetools"

	# Load the default definitions
	. "$mergetools/defaults"
	if ! test -f "$mergetools/$tool"
	then
		return 1
	fi

	# Load the redefined functions
	. "$mergetools/$tool"

	if merge_mode && ! can_merge
	then
		echo "error: '$tool' can not be used to resolve merges" >&2
		exit 1
	elif diff_mode && ! can_diff
	then
		echo "error: '$tool' can only be used to resolve merges" >&2
		exit 1
	fi
	return 0
}

get_merge_tool_cmd () {
	# Prints the custom command for a merge tool
	merge_tool="$1"
	if diff_mode
	then
		echo "$(git config difftool.$merge_tool.cmd ||
			git config mergetool.$merge_tool.cmd)"
	else
		echo "$(git config mergetool.$merge_tool.cmd)"
	fi
}

# Entry point for running tools
run_merge_tool () {
	# If GIT_PREFIX is empty then we cannot use it in tools
	# that expect to be able to chdir() to its value.
	GIT_PREFIX=${GIT_PREFIX:-.}
	export GIT_PREFIX

	merge_tool_path="$(get_merge_tool_path "$1")" || exit
	base_present="$2"
	status=0

	# Bring tool-specific functions into scope
	setup_tool "$1"

	if merge_mode
	then
		merge_cmd "$1"
	else
		diff_cmd "$1"
	fi
	return $status
}

guess_merge_tool () {
	if merge_mode
	then
		tools="tortoisemerge"
	else
		tools="kompare"
	fi
	if test -n "$DISPLAY"
	then
		if test -n "$GNOME_DESKTOP_SESSION_ID"
		then
			tools="meld opendiff kdiff3 tkdiff xxdiff $tools"
		else
			tools="opendiff kdiff3 tkdiff xxdiff meld $tools"
		fi
		tools="$tools gvimdiff diffuse ecmerge p4merge araxis bc3"
	fi
	case "${VISUAL:-$EDITOR}" in
	*vim*)
		tools="$tools vimdiff emerge"
		;;
	*)
		tools="$tools emerge vimdiff"
		;;
	esac
	echo >&2 "merge tool candidates: $tools"

	# Loop over each candidate and stop when a valid merge tool is found.
	for i in $tools
	do
		merge_tool_path="$(translate_merge_tool_path "$i")"
		if type "$merge_tool_path" >/dev/null 2>&1
		then
			echo "$i"
			return 0
		fi
	done

	echo >&2 "No known merge resolution program available."
	return 1
}

get_configured_merge_tool () {
	# Diff mode first tries diff.tool and falls back to merge.tool.
	# Merge mode only checks merge.tool
	if diff_mode
	then
		merge_tool=$(git config diff.tool || git config merge.tool)
	else
		merge_tool=$(git config merge.tool)
	fi
	if test -n "$merge_tool" && ! valid_tool "$merge_tool"
	then
		echo >&2 "git config option $TOOL_MODE.tool set to unknown tool: $merge_tool"
		echo >&2 "Resetting to default..."
		return 1
	fi
	echo "$merge_tool"
}

get_merge_tool_path () {
	# A merge tool has been set, so verify that it's valid.
	merge_tool="$1"
	if ! valid_tool "$merge_tool"
	then
		echo >&2 "Unknown merge tool $merge_tool"
		exit 1
	fi
	if diff_mode
	then
		merge_tool_path=$(git config difftool."$merge_tool".path ||
				  git config mergetool."$merge_tool".path)
	else
		merge_tool_path=$(git config mergetool."$merge_tool".path)
	fi
	if test -z "$merge_tool_path"
	then
		merge_tool_path="$(translate_merge_tool_path "$merge_tool")"
	fi
	if test -z "$(get_merge_tool_cmd "$merge_tool")" &&
		! type "$merge_tool_path" >/dev/null 2>&1
	then
		echo >&2 "The $TOOL_MODE tool $merge_tool is not available as"\
			 "'$merge_tool_path'"
		exit 1
	fi
	echo "$merge_tool_path"
}

get_merge_tool () {
	# Check if a merge tool has been configured
	merge_tool="$(get_configured_merge_tool)"
	# Try to guess an appropriate merge tool if no tool has been set.
	if test -z "$merge_tool"
	then
		merge_tool="$(guess_merge_tool)" || exit
	fi
	echo "$merge_tool"
}
