#!/bin/sh
# git-mergetool--lib is a library for common merge tool functions
MERGE_TOOLS_DIR=$(git --exec-path)/mergetools

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
	tool="$1"

	# Fallback definitions, to be overriden by tools.
	can_merge () {
		return 0
	}

	can_diff () {
		return 0
	}

	diff_cmd () {
		status=1
		return $status
	}

	merge_cmd () {
		status=1
		return $status
	}

	translate_merge_tool_path () {
		echo "$1"
	}

	if ! test -f "$MERGE_TOOLS_DIR/$tool"
	then
		# Use a special return code for this case since we want to
		# source "defaults" even when an explicit tool path is
		# configured since the user can use that to override the
		# default path in the scriptlet.
		return 2
	fi

	# Load the redefined functions
	. "$MERGE_TOOLS_DIR/$tool"

	if merge_mode && ! can_merge
	then
		echo "error: '$tool' can not be used to resolve merges" >&2
		return 1
	elif diff_mode && ! can_diff
	then
		echo "error: '$tool' can only be used to resolve merges" >&2
		return 1
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
	exitcode=$?
	case $exitcode in
	0)
		:
		;;
	2)
		# The configured tool is not a built-in tool.
		test -n "$merge_tool_path" || return 1
		;;
	*)
		return $exitcode
		;;
	esac

	if merge_mode
	then
		run_merge_cmd "$1"
	else
		run_diff_cmd "$1"
	fi
	return $status
}

# Run a either a configured or built-in diff tool
run_diff_cmd () {
	merge_tool_cmd="$(get_merge_tool_cmd "$1")"
	if test -n "$merge_tool_cmd"
	then
		( eval $merge_tool_cmd )
		status=$?
		return $status
	else
		diff_cmd "$1"
	fi
}

# Run a either a configured or built-in merge tool
run_merge_cmd () {
	merge_tool_cmd="$(get_merge_tool_cmd "$1")"
	if test -n "$merge_tool_cmd"
	then
		trust_exit_code="$(git config --bool \
			mergetool."$1".trustExitCode || echo false)"
		if test "$trust_exit_code" = "false"
		then
			touch "$BACKUP"
			( eval $merge_tool_cmd )
			status=$?
			check_unchanged
		else
			( eval $merge_tool_cmd )
			status=$?
		fi
		return $status
	else
		merge_cmd "$1"
	fi
}

list_merge_tool_candidates () {
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
		tools="$tools gvimdiff diffuse ecmerge p4merge araxis bc3 codecompare"
	fi
	case "${VISUAL:-$EDITOR}" in
	*vim*)
		tools="$tools vimdiff emerge"
		;;
	*)
		tools="$tools emerge vimdiff"
		;;
	esac
}

show_tool_help () {
	unavailable= available= LF='
'
	for i in "$MERGE_TOOLS_DIR"/*
	do
		tool=$(basename "$i")
		setup_tool "$tool" 2>/dev/null || continue

		merge_tool_path=$(translate_merge_tool_path "$tool")
		if type "$merge_tool_path" >/dev/null 2>&1
		then
			available="$available$tool$LF"
		else
			unavailable="$unavailable$tool$LF"
		fi
	done

	cmd_name=${TOOL_MODE}tool
	if test -n "$available"
	then
		echo "'git $cmd_name --tool=<tool>' may be set to one of the following:"
		echo "$available" | sort | sed -e 's/^/	/'
	else
		echo "No suitable tool for 'git $cmd_name --tool=<tool>' found."
	fi
	if test -n "$unavailable"
	then
		echo
		echo 'The following tools are valid, but not currently available:'
		echo "$unavailable" | sort | sed -e 's/^/	/'
	fi
	if test -n "$unavailable$available"
	then
		echo
		echo "Some of the tools listed above only work in a windowed"
		echo "environment. If run in a terminal-only session, they will fail."
	fi
	exit 0
}

guess_merge_tool () {
	list_merge_tool_candidates
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
