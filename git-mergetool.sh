#!/bin/sh
#
# This program resolves merge conflicts in git
#
# Copyright (c) 2006 Theodore Y. Ts'o
# Copyright (c) 2009-2016 David Aguilar
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Junio C Hamano.
#

USAGE='[--tool=tool] [--tool-help] [-y|--no-prompt|--prompt] [-g|--gui|--no-gui] [-O<orderfile>] [file to merge] ...'
SUBDIRECTORY_OK=Yes
NONGIT_OK=Yes
OPTIONS_SPEC=
TOOL_MODE=merge
. git-sh-setup
. git-mergetool--lib

# Returns true if the mode reflects a symlink
is_symlink () {
	test "$1" = 120000
}

is_submodule () {
	test "$1" = 160000
}

local_present () {
	test -n "$local_mode"
}

remote_present () {
	test -n "$remote_mode"
}

base_present () {
	test -n "$base_mode"
}

mergetool_tmpdir_init () {
	if test "$(git config --bool mergetool.writeToTemp)" != true
	then
		MERGETOOL_TMPDIR=.
		return 0
	fi
	if MERGETOOL_TMPDIR=$(mktemp -d -t "git-mergetool-XXXXXX" 2>/dev/null)
	then
		return 0
	fi
	die "error: mktemp is needed when 'mergetool.writeToTemp' is true"
}

cleanup_temp_files () {
	if test "$1" = --save-backup
	then
		rm -rf -- "$MERGED.orig"
		test -e "$BACKUP" && mv -- "$BACKUP" "$MERGED.orig"
		rm -f -- "$LOCAL" "$REMOTE" "$BASE"
	else
		rm -f -- "$LOCAL" "$REMOTE" "$BASE" "$BACKUP"
	fi
	if test "$MERGETOOL_TMPDIR" != "."
	then
		rmdir "$MERGETOOL_TMPDIR"
	fi
}

describe_file () {
	mode="$1"
	branch="$2"
	file="$3"

	printf "  {%s}: " "$branch"
	if test -z "$mode"
	then
		echo "deleted"
	elif is_symlink "$mode"
	then
		echo "a symbolic link -> '$(cat "$file")'"
	elif is_submodule "$mode"
	then
		echo "submodule commit $file"
	elif base_present
	then
		echo "modified file"
	else
		echo "created file"
	fi
}

resolve_symlink_merge () {
	while true
	do
		printf "Use (l)ocal or (r)emote, or (a)bort? "
		read ans || return 1
		case "$ans" in
		[lL]*)
			git checkout-index -f --stage=2 -- "$MERGED"
			git add -- "$MERGED"
			cleanup_temp_files --save-backup
			return 0
			;;
		[rR]*)
			git checkout-index -f --stage=3 -- "$MERGED"
			git add -- "$MERGED"
			cleanup_temp_files --save-backup
			return 0
			;;
		[aA]*)
			return 1
			;;
		esac
	done
}

resolve_deleted_merge () {
	while true
	do
		if base_present
		then
			printf "Use (m)odified or (d)eleted file, or (a)bort? "
		else
			printf "Use (c)reated or (d)eleted file, or (a)bort? "
		fi
		read ans || return 1
		case "$ans" in
		[mMcC]*)
			git add -- "$MERGED"
			if test "$merge_keep_backup" = "true"
			then
				cleanup_temp_files --save-backup
			else
				cleanup_temp_files
			fi
			return 0
			;;
		[dD]*)
			git rm -- "$MERGED" > /dev/null
			cleanup_temp_files
			return 0
			;;
		[aA]*)
			if test "$merge_keep_temporaries" = "false"
			then
				cleanup_temp_files
			fi
			return 1
			;;
		esac
	done
}

resolve_submodule_merge () {
	while true
	do
		printf "Use (l)ocal or (r)emote, or (a)bort? "
		read ans || return 1
		case "$ans" in
		[lL]*)
			if ! local_present
			then
				if test -n "$(git ls-tree HEAD -- "$MERGED")"
				then
					# Local isn't present, but it's a subdirectory
					git ls-tree --full-name -r HEAD -- "$MERGED" |
					git update-index --index-info || exit $?
				else
					test -e "$MERGED" && mv -- "$MERGED" "$BACKUP"
					git update-index --force-remove "$MERGED"
					cleanup_temp_files --save-backup
				fi
			elif is_submodule "$local_mode"
			then
				stage_submodule "$MERGED" "$local_sha1"
			else
				git checkout-index -f --stage=2 -- "$MERGED"
				git add -- "$MERGED"
			fi
			return 0
			;;
		[rR]*)
			if ! remote_present
			then
				if test -n "$(git ls-tree MERGE_HEAD -- "$MERGED")"
				then
					# Remote isn't present, but it's a subdirectory
					git ls-tree --full-name -r MERGE_HEAD -- "$MERGED" |
					git update-index --index-info || exit $?
				else
					test -e "$MERGED" && mv -- "$MERGED" "$BACKUP"
					git update-index --force-remove "$MERGED"
				fi
			elif is_submodule "$remote_mode"
			then
				! is_submodule "$local_mode" &&
				test -e "$MERGED" &&
				mv -- "$MERGED" "$BACKUP"
				stage_submodule "$MERGED" "$remote_sha1"
			else
				test -e "$MERGED" && mv -- "$MERGED" "$BACKUP"
				git checkout-index -f --stage=3 -- "$MERGED"
				git add -- "$MERGED"
			fi
			cleanup_temp_files --save-backup
			return 0
			;;
		[aA]*)
			return 1
			;;
		esac
	done
}

stage_submodule () {
	path="$1"
	submodule_sha1="$2"
	mkdir -p "$path" ||
	die "fatal: unable to create directory for module at $path"
	# Find $path relative to work tree
	work_tree_root=$(cd_to_toplevel && pwd)
	work_rel_path=$(cd "$path" &&
		GIT_WORK_TREE="${work_tree_root}" git rev-parse --show-prefix
	)
	test -n "$work_rel_path" ||
	die "fatal: unable to get path of module $path relative to work tree"
	git update-index --add --replace --cacheinfo 160000 "$submodule_sha1" "${work_rel_path%/}" || die
}

checkout_staged_file () {
	tmpfile="$(git checkout-index --temp --stage="$1" "$2" 2>/dev/null)" &&
	tmpfile=${tmpfile%%'	'*}

	if test $? -eq 0 && test -n "$tmpfile"
	then
		mv -- "$(git rev-parse --show-cdup)$tmpfile" "$3"
	else
		>"$3"
	fi
}

hide_resolved () {
	git merge-file --ours -q -p "$LOCAL" "$BASE" "$REMOTE" >"$LCONFL"
	git merge-file --theirs -q -p "$LOCAL" "$BASE" "$REMOTE" >"$RCONFL"
	mv -- "$LCONFL" "$LOCAL"
	mv -- "$RCONFL" "$REMOTE"
}

merge_file () {
	MERGED="$1"

	f=$(git ls-files -u -- "$MERGED")
	if test -z "$f"
	then
		if test ! -f "$MERGED"
		then
			echo "$MERGED: file not found"
		else
			echo "$MERGED: file does not need merging"
		fi
		return 1
	fi

	# extract file extension from the last path component
	case "${MERGED##*/}" in
	*.*)
		ext=.${MERGED##*.}
		BASE=${MERGED%"$ext"}
		;;
	*)
		BASE=$MERGED
		ext=
	esac

	initialize_merge_tool "$merge_tool" || return

	mergetool_tmpdir_init

	if test "$MERGETOOL_TMPDIR" != "."
	then
		# If we're using a temporary directory then write to the
		# top-level of that directory.
		BASE=${BASE##*/}
	fi

	BACKUP="$MERGETOOL_TMPDIR/${BASE}_BACKUP_$$$ext"
	LOCAL="$MERGETOOL_TMPDIR/${BASE}_LOCAL_$$$ext"
	LCONFL="$MERGETOOL_TMPDIR/${BASE}_LOCAL_LCONFL_$$$ext"
	REMOTE="$MERGETOOL_TMPDIR/${BASE}_REMOTE_$$$ext"
	RCONFL="$MERGETOOL_TMPDIR/${BASE}_REMOTE_RCONFL_$$$ext"
	BASE="$MERGETOOL_TMPDIR/${BASE}_BASE_$$$ext"

	base_mode= local_mode= remote_mode=

	# here, $IFS is just a LF
	for line in $f
	do
		mode=${line%% *}		# 1st word
		sha1=${line#"$mode "}
		sha1=${sha1%% *}		# 2nd word
		case "${line#$mode $sha1 }" in	# remainder
		'1	'*)
			base_mode=$mode
			;;
		'2	'*)
			local_mode=$mode local_sha1=$sha1
			;;
		'3	'*)
			remote_mode=$mode remote_sha1=$sha1
			;;
		esac
	done

	if is_submodule "$local_mode" || is_submodule "$remote_mode"
	then
		echo "Submodule merge conflict for '$MERGED':"
		describe_file "$local_mode" "local" "$local_sha1"
		describe_file "$remote_mode" "remote" "$remote_sha1"
		resolve_submodule_merge
		return
	fi

	if test -f "$MERGED"
	then
		mv -- "$MERGED" "$BACKUP"
		cp -- "$BACKUP" "$MERGED"
	fi
	# Create a parent directory to handle delete/delete conflicts
	# where the base's directory no longer exists.
	mkdir -p "$(dirname "$MERGED")"

	checkout_staged_file 1 "$MERGED" "$BASE"
	checkout_staged_file 2 "$MERGED" "$LOCAL"
	checkout_staged_file 3 "$MERGED" "$REMOTE"

	# hideResolved preferences hierarchy.
	global_config="mergetool.hideResolved"
	tool_config="mergetool.${merge_tool}.hideResolved"

	if enabled=$(git config --type=bool "$tool_config")
	then
		# The user has a specific preference for a specific tool and no
		# other preferences should override that.
		: ;
	elif enabled=$(git config --type=bool "$global_config")
	then
		# The user has a general preference for all tools.
		#
		# 'true' means the user likes the feature so we should use it
		# where possible but tool authors can still override.
		#
		# 'false' means the user doesn't like the feature so we should
		# not use it anywhere.
		if test "$enabled" = true && hide_resolved_enabled
		then
		    enabled=true
		else
		    enabled=false
		fi
	else
		# The user does not have a preference. Default to disabled.
		enabled=false
	fi

	if test "$enabled" = true
	then
		hide_resolved
	fi

	if test -z "$local_mode" || test -z "$remote_mode"
	then
		echo "Deleted merge conflict for '$MERGED':"
		describe_file "$local_mode" "local" "$LOCAL"
		describe_file "$remote_mode" "remote" "$REMOTE"
		resolve_deleted_merge
		status=$?
		rmdir -p "$(dirname "$MERGED")" 2>/dev/null
		return $status
	fi

	if is_symlink "$local_mode" || is_symlink "$remote_mode"
	then
		echo "Symbolic link merge conflict for '$MERGED':"
		describe_file "$local_mode" "local" "$LOCAL"
		describe_file "$remote_mode" "remote" "$REMOTE"
		resolve_symlink_merge
		return
	fi

	echo "Normal merge conflict for '$MERGED':"
	describe_file "$local_mode" "local" "$LOCAL"
	describe_file "$remote_mode" "remote" "$REMOTE"
	if test "$guessed_merge_tool" = true || test "$prompt" = true
	then
		printf "Hit return to start merge resolution tool (%s): " "$merge_tool"
		read ans || return 1
	fi

	if base_present
	then
		present=true
	else
		present=false
	fi

	if ! run_merge_tool "$merge_tool" "$present"
	then
		echo "merge of $MERGED failed" 1>&2
		mv -- "$BACKUP" "$MERGED"

		if test "$merge_keep_temporaries" = "false"
		then
			cleanup_temp_files
		fi

		return 1
	fi

	if test "$merge_keep_backup" = "true"
	then
		mv -- "$BACKUP" "$MERGED.orig"
	else
		rm -- "$BACKUP"
	fi

	git add -- "$MERGED"
	cleanup_temp_files
	return 0
}

prompt_after_failed_merge () {
	while true
	do
		printf "Continue merging other unresolved paths [y/n]? "
		read ans || return 1
		case "$ans" in
		[yY]*)
			return 0
			;;
		[nN]*)
			return 1
			;;
		esac
	done
}

print_noop_and_exit () {
	echo "No files need merging"
	exit 0
}

main () {
	prompt=$(git config --bool mergetool.prompt)
	GIT_MERGETOOL_GUI=
	guessed_merge_tool=false
	orderfile=

	while test $# != 0
	do
		case "$1" in
		--tool-help=*)
			TOOL_MODE=${1#--tool-help=}
			show_tool_help
			;;
		--tool-help)
			show_tool_help
			;;
		-t|--tool*)
			case "$#,$1" in
			*,*=*)
				merge_tool=${1#*=}
				;;
			1,*)
				usage ;;
			*)
				merge_tool="$2"
				shift ;;
			esac
			;;
		--no-gui)
			GIT_MERGETOOL_GUI=false
			;;
		-g|--gui)
			GIT_MERGETOOL_GUI=true
			;;
		-y|--no-prompt)
			prompt=false
			;;
		--prompt)
			prompt=true
			;;
		-O*)
			orderfile="${1#-O}"
			;;
		--)
			shift
			break
			;;
		-*)
			usage
			;;
		*)
			break
			;;
		esac
		shift
	done

	git_dir_init
	require_work_tree

	if test -z "$merge_tool"
	then
		merge_tool=$(get_merge_tool)
		subshell_exit_status=$?
		if test $subshell_exit_status = 1
		then
			guessed_merge_tool=true
		elif test $subshell_exit_status -gt 1
		then
			exit $subshell_exit_status
		fi
	fi
	merge_keep_backup="$(git config --bool mergetool.keepBackup || echo true)"
	merge_keep_temporaries="$(git config --bool mergetool.keepTemporaries || echo false)"

	prefix=$(git rev-parse --show-prefix) || exit 1
	cd_to_toplevel

	if test -n "$orderfile"
	then
		orderfile=$(
			git rev-parse --prefix "$prefix" -- "$orderfile" |
			sed -e 1d
		)
	fi

	if test $# -eq 0 && test -e "$GIT_DIR/MERGE_RR"
	then
		set -- $(git rerere remaining)
		if test $# -eq 0
		then
			print_noop_and_exit
		fi
	elif test $# -ge 0
	then
		# rev-parse provides the -- needed for 'set'
		eval "set $(git rev-parse --sq --prefix "$prefix" -- "$@")"
	fi

	files=$(git -c core.quotePath=false \
		diff --name-only --diff-filter=U \
		${orderfile:+"-O$orderfile"} -- "$@")

	if test -z "$files"
	then
		print_noop_and_exit
	fi

	printf "Merging:\n"
	printf "%s\n" "$files"

	rc=0
	set -- $files
	while test $# -ne 0
	do
		printf "\n"
		if ! merge_file "$1"
		then
			rc=1
			test $# -ne 1 && prompt_after_failed_merge || exit 1
		fi
		shift
	done

	exit $rc
}

main "$@"
