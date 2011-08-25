diff_cmd () {
	case "$1" in
	gvimdiff|vimdiff)
		"$merge_tool_path" -R -f -d \
			-c 'wincmd l' -c 'cd $GIT_PREFIX' "$LOCAL" "$REMOTE"
		;;
	gvimdiff2|vimdiff2)
		"$merge_tool_path" -R -f -d \
			-c 'wincmd l' -c 'cd $GIT_PREFIX' "$LOCAL" "$REMOTE"
		;;
	esac
}

merge_cmd () {
	touch "$BACKUP"
	case "$1" in
	gvimdiff|vimdiff)
		if $base_present
		then
			"$merge_tool_path" -f -d -c 'wincmd J' \
				"$MERGED" "$LOCAL" "$BASE" "$REMOTE"
		else
			"$merge_tool_path" -f -d -c 'wincmd l' \
				"$LOCAL" "$MERGED" "$REMOTE"
		fi
		;;
	gvimdiff2|vimdiff2)
		"$merge_tool_path" -f -d -c 'wincmd l' \
			"$LOCAL" "$MERGED" "$REMOTE"
		;;
	esac
	check_unchanged
}

translate_merge_tool_path() {
	case "$1" in
	gvimdiff|gvimdiff2)
		echo gvim
		;;
	vimdiff|vimdiff2)
		echo vim
		;;
	esac
}
