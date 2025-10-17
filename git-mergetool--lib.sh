# git-mergetool--lib is a shell library for common merge tool functions

: ${MERGE_TOOLS_DIR=$(git --exec-path)/mergetools}

IFS='
'

mode_ok () {
	if diff_mode
	then
		can_diff
	elif merge_mode
	then
		can_merge
	else
		false
	fi
}

is_available () {
	merge_tool_path=$(translate_merge_tool_path "$1") &&
	type "$merge_tool_path" >/dev/null 2>&1
}

list_config_tools () {
	section=$1
	line_prefix=${2:-}

	git config --get-regexp $section'\..*\.cmd' |
	while read -r key value
	do
		toolname=${key#$section.}
		toolname=${toolname%.cmd}

		printf "%s%s\n" "$line_prefix" "$toolname"
	done
}

show_tool_names () {
	condition=${1:-true} per_line_prefix=${2:-} preamble=${3:-}
	not_found_msg=${4:-}
	extra_content=${5:-}

	shown_any=
	( cd "$MERGE_TOOLS_DIR" && ls ) | {
		while read scriptname
		do
			setup_tool "$scriptname" 2>/dev/null
			# We need an actual line feed here
			variants="$variants
$(list_tool_variants)"
		done
		variants="$(echo "$variants" | sort -u)"

		for toolname in $variants
		do
			if setup_tool "$toolname" 2>/dev/null &&
				(eval "$condition" "$toolname")
			then
				if test -n "$preamble"
				then
					printf "%s\n" "$preamble"
					preamble=
				fi
				shown_any=yes
				printf "%s%-15s  %s\n" "$per_line_prefix" "$toolname" $(diff_mode && diff_cmd_help "$toolname" || merge_cmd_help "$toolname")
			fi
		done

		if test -n "$extra_content"
		then
			if test -n "$preamble"
			then
				# Note: no '\n' here since we don't want a
				# blank line if there is no initial content.
				printf "%s" "$preamble"
				preamble=
			fi
			shown_any=yes
			printf "\n%s\n" "$extra_content"
		fi

		if test -n "$preamble" && test -n "$not_found_msg"
		then
			printf "%s\n" "$not_found_msg"
		fi

		test -n "$shown_any"
	}
}

diff_mode () {
	test "$TOOL_MODE" = diff
}

merge_mode () {
	test "$TOOL_MODE" = merge
}

get_gui_default () {
	if diff_mode
	then
		GUI_DEFAULT_KEY="difftool.guiDefault"
	else
		GUI_DEFAULT_KEY="mergetool.guiDefault"
	fi
	GUI_DEFAULT_CONFIG_LCASE=$(git config --default false --get "$GUI_DEFAULT_KEY" | tr 'A-Z' 'a-z')
	if test "$GUI_DEFAULT_CONFIG_LCASE" = "auto"
	then
		if test -n "$DISPLAY"
		then
			GUI_DEFAULT=true
		else
			GUI_DEFAULT=false
		fi
	else
		GUI_DEFAULT=$(git config --default false --bool --get "$GUI_DEFAULT_KEY")
		subshell_exit_status=$?
		if test $subshell_exit_status -ne 0
		then
			exit $subshell_exit_status
		fi
	fi
	echo $GUI_DEFAULT
}

gui_mode () {
	if test -z "$GIT_MERGETOOL_GUI"
	then
		GIT_MERGETOOL_GUI=$(get_gui_default)
		if test $? -ne 0
		then
			exit 2
		fi
	fi
	test "$GIT_MERGETOOL_GUI" = true
}

translate_merge_tool_path () {
	echo "$1"
}

check_unchanged () {
	if test "$MERGED" -nt "$BACKUP"
	then
		return 0
	else
		while true
		do
			echo "$MERGED seems unchanged."
			printf "Was the merge successful [y/n]? "
			read answer || return 1
			case "$answer" in
			y*|Y*) return 0 ;;
			n*|N*) return 1 ;;
			esac
		done
	fi
}

valid_tool () {
	setup_tool "$1" 2>/dev/null && return 0
	cmd=$(get_merge_tool_cmd "$1")
	test -n "$cmd"
}

setup_user_tool () {
	merge_tool_cmd=$(get_merge_tool_cmd "$tool")
	test -n "$merge_tool_cmd" || return 1

	diff_cmd () {
		( eval $merge_tool_cmd )
	}

	merge_cmd () {
		( eval $merge_tool_cmd )
	}

	list_tool_variants () {
		echo "$tool"
	}
}

setup_tool () {
	tool="$1"

	# Fallback definitions, to be overridden by tools.
	can_merge () {
		return 0
	}

	can_diff () {
		return 0
	}

	diff_cmd () {
		return 1
	}

	diff_cmd_help () {
		return 0
	}

	merge_cmd () {
		return 1
	}

	merge_cmd_help () {
		return 0
	}

	hide_resolved_enabled () {
		return 0
	}

	translate_merge_tool_path () {
		echo "$1"
	}

	list_tool_variants () {
		echo "$tool"
	}

	# Most tools' exit codes cannot be trusted, so By default we ignore
	# their exit code and check the merged file's modification time in
	# check_unchanged() to determine whether or not the merge was
	# successful.  The return value from run_merge_cmd, by default, is
	# determined by check_unchanged().
	#
	# When a tool's exit code can be trusted then the return value from
	# run_merge_cmd is simply the tool's exit code, and check_unchanged()
	# is not called.
	#
	# The return value of exit_code_trustable() tells us whether or not we
	# can trust the tool's exit code.
	#
	# User-defined and built-in tools default to false.
	# Built-in tools advertise that their exit code is trustable by
	# redefining exit_code_trustable() to true.

	exit_code_trustable () {
		false
	}

	if test -f "$MERGE_TOOLS_DIR/$tool"
	then
		. "$MERGE_TOOLS_DIR/$tool"
	elif test -f "$MERGE_TOOLS_DIR/${tool%[0-9]}"
	then
		. "$MERGE_TOOLS_DIR/${tool%[0-9]}"
	else
		setup_user_tool
		rc=$?
		if test $rc -ne 0
		then
			echo >&2 "error: ${TOOL_MODE}tool.$tool.cmd not set for tool '$tool'"
		fi
		return $rc
	fi

	# Now let the user override the default command for the tool.  If
	# they have not done so then this will return 1 which we ignore.
	setup_user_tool

	if ! list_tool_variants | grep -q "^$tool$"
	then
		echo "error: unknown tool variant '$tool'" >&2
		return 1
	fi

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
	merge_tool="$1"
	if diff_mode
	then
		git config "difftool.$merge_tool.cmd" ||
		git config "mergetool.$merge_tool.cmd"
	else
		git config "mergetool.$merge_tool.cmd"
	fi
}

trust_exit_code () {
	if git config --bool "mergetool.$1.trustExitCode"
	then
		:; # OK
	elif exit_code_trustable
	then
		echo true
	else
		echo false
	fi
}

initialize_merge_tool () {
	# Bring tool-specific functions into scope
	setup_tool "$1" || return 1
}

# Entry point for running tools
run_merge_tool () {
	# If GIT_PREFIX is empty then we cannot use it in tools
	# that expect to be able to chdir() to its value.
	GIT_PREFIX=${GIT_PREFIX:-.}
	export GIT_PREFIX

	merge_tool_path=$(get_merge_tool_path "$1") || exit
	base_present="$2"

	if merge_mode
	then
		run_merge_cmd "$1"
	else
		run_diff_cmd "$1"
	fi
}

# Run a either a configured or built-in diff tool
run_diff_cmd () {
	diff_cmd "$1"
}

# Run a either a configured or built-in merge tool
run_merge_cmd () {
	mergetool_trust_exit_code=$(trust_exit_code "$1")
	if test "$mergetool_trust_exit_code" = "true"
	then
		merge_cmd "$1"
	else
		touch "$BACKUP"
		merge_cmd "$1"
		check_unchanged
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
		tools="$tools gvimdiff diffuse diffmerge ecmerge"
		tools="$tools p4merge araxis bc codecompare"
		tools="$tools smerge"
	fi
	case "${VISUAL:-$EDITOR}" in
	*nvim*)
		tools="$tools nvimdiff vimdiff emerge"
		;;
	*vim*)
		tools="$tools vimdiff nvimdiff emerge"
		;;
	*)
		tools="$tools emerge vimdiff nvimdiff"
		;;
	esac
}

show_tool_help () {
	tool_opt="'git ${TOOL_MODE}tool --tool=<tool>'"

	tab='	'
	LF='
'
	any_shown=no

	cmd_name=${TOOL_MODE}tool
	config_tools=$({
		diff_mode && list_config_tools difftool "$tab$tab"
		list_config_tools mergetool "$tab$tab"
	} | sort)
	extra_content=
	if test -n "$config_tools"
	then
		extra_content="${tab}user-defined:${LF}$config_tools"
	fi

	show_tool_names 'mode_ok && is_available' "$tab$tab" \
		"$tool_opt may be set to one of the following:" \
		"No suitable tool for 'git $cmd_name --tool=<tool>' found." \
		"$extra_content" &&
		any_shown=yes

	show_tool_names 'mode_ok && ! is_available' "$tab$tab" \
		"${LF}The following tools are valid, but not currently available:" &&
		any_shown=yes

	if test "$any_shown" = yes
	then
		echo
		echo "Some of the tools listed above only work in a windowed"
		echo "environment. If run in a terminal-only session, they will fail."
	fi
	exit 0
}

guess_merge_tool () {
	list_merge_tool_candidates
	cat >&2 <<-EOF

	This message is displayed because '$TOOL_MODE.tool' is not configured.
	See 'git ${TOOL_MODE}tool --tool-help' or 'git help config' for more details.
	'git ${TOOL_MODE}tool' will now attempt to use one of the following tools:
	$tools
	EOF

	# Loop over each candidate and stop when a valid merge tool is found.
	IFS=' '
	for tool in $tools
	do
		is_available "$tool" && echo "$tool" && return 0
	done

	echo >&2 "No known ${TOOL_MODE} tool is available."
	return 1
}

get_configured_merge_tool () {
	keys=
	if diff_mode
	then
		if gui_mode
		then
			keys="diff.guitool merge.guitool diff.tool merge.tool"
		else
			keys="diff.tool merge.tool"
		fi
	else
		if gui_mode
		then
			keys="merge.guitool merge.tool"
		else
			keys="merge.tool"
		fi
	fi

	merge_tool=$(
		IFS=' '
		for key in $keys
		do
			selected=$(git config $key)
			if test -n "$selected"
			then
				echo "$selected"
				return
			fi
		done)

	if test -n "$merge_tool" && ! valid_tool "$merge_tool"
	then
		echo >&2 "git config option $TOOL_MODE.${gui_prefix}tool set to unknown tool: $merge_tool"
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
		echo >&2 "Unknown $TOOL_MODE tool $merge_tool"
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
		merge_tool_path=$(translate_merge_tool_path "$merge_tool")
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
	is_guessed=false
	# Check if a merge tool has been configured
	merge_tool=$(get_configured_merge_tool)
	subshell_exit_status=$?
	if test $subshell_exit_status -gt "1"
	then
		exit $subshell_exit_status
	fi
	# Try to guess an appropriate merge tool if no tool has been set.
	if test -z "$merge_tool"
	then
		merge_tool=$(guess_merge_tool) || exit
		is_guessed=true
	fi
	echo "$merge_tool"
	test "$is_guessed" = false
}

mergetool_find_win32_cmd () {
	executable=$1
	sub_directory=$2

	# Use $executable if it exists in $PATH
	if type -p "$executable" >/dev/null 2>&1
	then
		printf '%s' "$executable"
		return
	fi

	# Look for executable in the typical locations
	for directory in $(env | grep -Ei '^PROGRAM(FILES(\(X86\))?|W6432)=' |
		cut -d '=' -f 2- | sort -u)
	do
		if test -n "$directory" && test -x "$directory/$sub_directory/$executable"
		then
			printf '%s' "$directory/$sub_directory/$executable"
			return
		fi
	done

	printf '%s' "$executable"
}
