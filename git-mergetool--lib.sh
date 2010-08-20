#!/bin/sh
# git-mergetool--lib is a library for common merge tool functions
diff_mode() {
	test "$TOOL_MODE" = diff
}

merge_mode() {
	test "$TOOL_MODE" = merge
}

translate_merge_tool_path () {
	case "$1" in
	vimdiff)
		echo vim
		;;
	gvimdiff)
		echo gvim
		;;
	emerge)
		echo emacs
		;;
	araxis)
		echo compare
		;;
	*)
		echo "$1"
		;;
	esac
}

check_unchanged () {
	if test "$MERGED" -nt "$BACKUP"; then
		status=0
	else
		while true; do
			echo "$MERGED seems unchanged."
			printf "Was the merge successful? [y/n] "
			read answer
			case "$answer" in
			y*|Y*) status=0; break ;;
			n*|N*) status=1; break ;;
			esac
		done
	fi
}

valid_tool () {
	case "$1" in
	kdiff3 | tkdiff | xxdiff | meld | opendiff | \
	emerge | vimdiff | gvimdiff | ecmerge | diffuse | araxis | p4merge)
		;; # happy
	tortoisemerge)
		if ! merge_mode; then
			return 1
		fi
		;;
	kompare)
		if ! diff_mode; then
			return 1
		fi
		;;
	*)
		if test -z "$(get_merge_tool_cmd "$1")"; then
			return 1
		fi
		;;
	esac
}

get_merge_tool_cmd () {
	# Prints the custom command for a merge tool
	if test -n "$1"; then
		merge_tool="$1"
	else
		merge_tool="$(get_merge_tool)"
	fi
	if diff_mode; then
		echo "$(git config difftool.$merge_tool.cmd ||
		        git config mergetool.$merge_tool.cmd)"
	else
		echo "$(git config mergetool.$merge_tool.cmd)"
	fi
}

run_merge_tool () {
	merge_tool_path="$(get_merge_tool_path "$1")" || exit
	base_present="$2"
	status=0

	case "$1" in
	kdiff3)
		if merge_mode; then
			if $base_present; then
				("$merge_tool_path" --auto \
					--L1 "$MERGED (Base)" \
					--L2 "$MERGED (Local)" \
					--L3 "$MERGED (Remote)" \
					-o "$MERGED" \
					"$BASE" "$LOCAL" "$REMOTE" \
				> /dev/null 2>&1)
			else
				("$merge_tool_path" --auto \
					--L1 "$MERGED (Local)" \
					--L2 "$MERGED (Remote)" \
					-o "$MERGED" \
					"$LOCAL" "$REMOTE" \
				> /dev/null 2>&1)
			fi
			status=$?
		else
			("$merge_tool_path" --auto \
				--L1 "$MERGED (A)" \
				--L2 "$MERGED (B)" "$LOCAL" "$REMOTE" \
			> /dev/null 2>&1)
		fi
		;;
	kompare)
		"$merge_tool_path" "$LOCAL" "$REMOTE"
		;;
	tkdiff)
		if merge_mode; then
			if $base_present; then
				"$merge_tool_path" -a "$BASE" \
					-o "$MERGED" "$LOCAL" "$REMOTE"
			else
				"$merge_tool_path" \
					-o "$MERGED" "$LOCAL" "$REMOTE"
			fi
			status=$?
		else
			"$merge_tool_path" "$LOCAL" "$REMOTE"
		fi
		;;
	p4merge)
		if merge_mode; then
		    touch "$BACKUP"
			if $base_present; then
				"$merge_tool_path" "$BASE" "$LOCAL" "$REMOTE" "$MERGED"
			else
				"$merge_tool_path" "$LOCAL" "$LOCAL" "$REMOTE" "$MERGED"
			fi
			check_unchanged
		else
			"$merge_tool_path" "$LOCAL" "$REMOTE"
		fi
		;;
	meld)
		if merge_mode; then
			touch "$BACKUP"
			"$merge_tool_path" "$LOCAL" "$MERGED" "$REMOTE"
			check_unchanged
		else
			"$merge_tool_path" "$LOCAL" "$REMOTE"
		fi
		;;
	diffuse)
		if merge_mode; then
			touch "$BACKUP"
			if $base_present; then
				"$merge_tool_path" \
					"$LOCAL" "$MERGED" "$REMOTE" \
					"$BASE" | cat
			else
				"$merge_tool_path" \
					"$LOCAL" "$MERGED" "$REMOTE" | cat
			fi
			check_unchanged
		else
			"$merge_tool_path" "$LOCAL" "$REMOTE" | cat
		fi
		;;
	vimdiff)
		if merge_mode; then
			touch "$BACKUP"
			"$merge_tool_path" -d -c "wincmd l" \
				"$LOCAL" "$MERGED" "$REMOTE"
			check_unchanged
		else
			"$merge_tool_path" -d -c "wincmd l" \
				"$LOCAL" "$REMOTE"
		fi
		;;
	gvimdiff)
		if merge_mode; then
			touch "$BACKUP"
			"$merge_tool_path" -d -c "wincmd l" -f \
				"$LOCAL" "$MERGED" "$REMOTE"
			check_unchanged
		else
			"$merge_tool_path" -d -c "wincmd l" -f \
				"$LOCAL" "$REMOTE"
		fi
		;;
	xxdiff)
		if merge_mode; then
			touch "$BACKUP"
			if $base_present; then
				"$merge_tool_path" -X --show-merged-pane \
					-R 'Accel.SaveAsMerged: "Ctrl-S"' \
					-R 'Accel.Search: "Ctrl+F"' \
					-R 'Accel.SearchForward: "Ctrl-G"' \
					--merged-file "$MERGED" \
					"$LOCAL" "$BASE" "$REMOTE"
			else
				"$merge_tool_path" -X $extra \
					-R 'Accel.SaveAsMerged: "Ctrl-S"' \
					-R 'Accel.Search: "Ctrl+F"' \
					-R 'Accel.SearchForward: "Ctrl-G"' \
					--merged-file "$MERGED" \
					"$LOCAL" "$REMOTE"
			fi
			check_unchanged
		else
			"$merge_tool_path" \
				-R 'Accel.Search: "Ctrl+F"' \
				-R 'Accel.SearchForward: "Ctrl-G"' \
				"$LOCAL" "$REMOTE"
		fi
		;;
	opendiff)
		if merge_mode; then
			touch "$BACKUP"
			if $base_present; then
				"$merge_tool_path" "$LOCAL" "$REMOTE" \
					-ancestor "$BASE" \
					-merge "$MERGED" | cat
			else
				"$merge_tool_path" "$LOCAL" "$REMOTE" \
					-merge "$MERGED" | cat
			fi
			check_unchanged
		else
			"$merge_tool_path" "$LOCAL" "$REMOTE" | cat
		fi
		;;
	ecmerge)
		if merge_mode; then
			touch "$BACKUP"
			if $base_present; then
				"$merge_tool_path" "$BASE" "$LOCAL" "$REMOTE" \
					--default --mode=merge3 --to="$MERGED"
			else
				"$merge_tool_path" "$LOCAL" "$REMOTE" \
					--default --mode=merge2 --to="$MERGED"
			fi
			check_unchanged
		else
			"$merge_tool_path" --default --mode=diff2 \
				"$LOCAL" "$REMOTE"
		fi
		;;
	emerge)
		if merge_mode; then
			if $base_present; then
				"$merge_tool_path" \
					-f emerge-files-with-ancestor-command \
					"$LOCAL" "$REMOTE" "$BASE" \
					"$(basename "$MERGED")"
			else
				"$merge_tool_path" \
					-f emerge-files-command \
					"$LOCAL" "$REMOTE" \
					"$(basename "$MERGED")"
			fi
			status=$?
		else
			"$merge_tool_path" -f emerge-files-command \
				"$LOCAL" "$REMOTE"
		fi
		;;
	tortoisemerge)
		if $base_present; then
			touch "$BACKUP"
			"$merge_tool_path" \
				-base:"$BASE" -mine:"$LOCAL" \
				-theirs:"$REMOTE" -merged:"$MERGED"
			check_unchanged
		else
			echo "TortoiseMerge cannot be used without a base" 1>&2
			status=1
		fi
		;;
	araxis)
		if merge_mode; then
			touch "$BACKUP"
			if $base_present; then
				"$merge_tool_path" -wait -merge -3 -a1 \
					"$BASE" "$LOCAL" "$REMOTE" "$MERGED" \
					>/dev/null 2>&1
			else
				"$merge_tool_path" -wait -2 \
					"$LOCAL" "$REMOTE" "$MERGED" \
					>/dev/null 2>&1
			fi
			check_unchanged
		else
			"$merge_tool_path" -wait -2 "$LOCAL" "$REMOTE" \
				>/dev/null 2>&1
		fi
		;;
	*)
		merge_tool_cmd="$(get_merge_tool_cmd "$1")"
		if test -z "$merge_tool_cmd"; then
			if merge_mode; then
				status=1
			fi
			break
		fi
		if merge_mode; then
			trust_exit_code="$(git config --bool \
				mergetool."$1".trustExitCode || echo false)"
			if test "$trust_exit_code" = "false"; then
				touch "$BACKUP"
				( eval $merge_tool_cmd )
				check_unchanged
			else
				( eval $merge_tool_cmd )
				status=$?
			fi
		else
			( eval $merge_tool_cmd )
		fi
		;;
	esac
	return $status
}

guess_merge_tool () {
	if merge_mode; then
		tools="tortoisemerge"
	else
		tools="kompare"
	fi
	if test -n "$DISPLAY"; then
		if test -n "$GNOME_DESKTOP_SESSION_ID" ; then
			tools="meld opendiff kdiff3 tkdiff xxdiff $tools"
		else
			tools="opendiff kdiff3 tkdiff xxdiff meld $tools"
		fi
		tools="$tools gvimdiff diffuse ecmerge p4merge araxis"
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
		if type "$merge_tool_path" > /dev/null 2>&1; then
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
	if diff_mode; then
		merge_tool=$(git config diff.tool || git config merge.tool)
	else
		merge_tool=$(git config merge.tool)
	fi
	if test -n "$merge_tool" && ! valid_tool "$merge_tool"; then
		echo >&2 "git config option $TOOL_MODE.tool set to unknown tool: $merge_tool"
		echo >&2 "Resetting to default..."
		return 1
	fi
	echo "$merge_tool"
}

get_merge_tool_path () {
	# A merge tool has been set, so verify that it's valid.
	if test -n "$1"; then
		merge_tool="$1"
	else
		merge_tool="$(get_merge_tool)"
	fi
	if ! valid_tool "$merge_tool"; then
		echo >&2 "Unknown merge tool $merge_tool"
		exit 1
	fi
	if diff_mode; then
		merge_tool_path=$(git config difftool."$merge_tool".path ||
		                  git config mergetool."$merge_tool".path)
	else
		merge_tool_path=$(git config mergetool."$merge_tool".path)
	fi
	if test -z "$merge_tool_path"; then
		merge_tool_path="$(translate_merge_tool_path "$merge_tool")"
	fi
	if test -z "$(get_merge_tool_cmd "$merge_tool")" &&
	! type "$merge_tool_path" > /dev/null 2>&1; then
		echo >&2 "The $TOOL_MODE tool $merge_tool is not available as"\
		         "'$merge_tool_path'"
		exit 1
	fi
	echo "$merge_tool_path"
}

get_merge_tool () {
	# Check if a merge tool has been configured
	merge_tool=$(get_configured_merge_tool)
	# Try to guess an appropriate merge tool if no tool has been set.
	if test -z "$merge_tool"; then
		merge_tool="$(guess_merge_tool)" || exit
	fi
	echo "$merge_tool"
}
