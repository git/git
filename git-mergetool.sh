#!/bin/sh
#
# This program resolves merge conflicts in git
#
# Copyright (c) 2006 Theodore Y. Ts'o
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Junio C Hamano.
#

USAGE='[--tool=tool] [-y|--no-prompt|--prompt] [file to merge] ...'
SUBDIRECTORY_OK=Yes
OPTIONS_SPEC=
TOOL_MODE=merge
. git-sh-setup
. git-mergetool--lib
require_work_tree

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

cleanup_temp_files () {
    if test "$1" = --save-backup ; then
	rm -rf -- "$MERGED.orig"
	test -e "$BACKUP" && mv -- "$BACKUP" "$MERGED.orig"
	rm -f -- "$LOCAL" "$REMOTE" "$BASE"
    else
	rm -f -- "$LOCAL" "$REMOTE" "$BASE" "$BACKUP"
    fi
}

describe_file () {
    mode="$1"
    branch="$2"
    file="$3"

    printf "  {%s}: " "$branch"
    if test -z "$mode"; then
	echo "deleted"
    elif is_symlink "$mode" ; then
	echo "a symbolic link -> '$(cat "$file")'"
    elif is_submodule "$mode" ; then
	echo "submodule commit $file"
    else
	if base_present; then
	    echo "modified file"
	else
	    echo "created file"
	fi
    fi
}


resolve_symlink_merge () {
    while true; do
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
    while true; do
	if base_present; then
	    printf "Use (m)odified or (d)eleted file, or (a)bort? "
	else
	    printf "Use (c)reated or (d)eleted file, or (a)bort? "
	fi
	read ans || return 1
	case "$ans" in
	    [mMcC]*)
		git add -- "$MERGED"
		cleanup_temp_files --save-backup
		return 0
		;;
	    [dD]*)
		git rm -- "$MERGED" > /dev/null
		cleanup_temp_files
		return 0
		;;
	    [aA]*)
		return 1
		;;
	    esac
	done
}

resolve_submodule_merge () {
    while true; do
	printf "Use (l)ocal or (r)emote, or (a)bort? "
	read ans || return 1
	case "$ans" in
	    [lL]*)
		if ! local_present; then
		    if test -n "$(git ls-tree HEAD -- "$MERGED")"; then
			# Local isn't present, but it's a subdirectory
			git ls-tree --full-name -r HEAD -- "$MERGED" | git update-index --index-info || exit $?
		    else
			test -e "$MERGED" && mv -- "$MERGED" "$BACKUP"
			git update-index --force-remove "$MERGED"
			cleanup_temp_files --save-backup
		    fi
		elif is_submodule "$local_mode"; then
		    stage_submodule "$MERGED" "$local_sha1"
		else
		    git checkout-index -f --stage=2 -- "$MERGED"
		    git add -- "$MERGED"
		fi
		return 0
		;;
	    [rR]*)
		if ! remote_present; then
		    if test -n "$(git ls-tree MERGE_HEAD -- "$MERGED")"; then
			# Remote isn't present, but it's a subdirectory
			git ls-tree --full-name -r MERGE_HEAD -- "$MERGED" | git update-index --index-info || exit $?
		    else
			test -e "$MERGED" && mv -- "$MERGED" "$BACKUP"
			git update-index --force-remove "$MERGED"
		    fi
		elif is_submodule "$remote_mode"; then
		    ! is_submodule "$local_mode" && test -e "$MERGED" && mv -- "$MERGED" "$BACKUP"
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
    mkdir -p "$path" || die "fatal: unable to create directory for module at $path"
    # Find $path relative to work tree
    work_tree_root=$(cd_to_toplevel && pwd)
    work_rel_path=$(cd "$path" && GIT_WORK_TREE="${work_tree_root}" git rev-parse --show-prefix)
    test -n "$work_rel_path" || die "fatal: unable to get path of module $path relative to work tree"
    git update-index --add --replace --cacheinfo 160000 "$submodule_sha1" "${work_rel_path%/}" || die
}

checkout_staged_file () {
    tmpfile=$(expr "$(git checkout-index --temp --stage="$1" "$2")" : '\([^	]*\)	')

    if test $? -eq 0 -a -n "$tmpfile" ; then
	mv -- "$(git rev-parse --show-cdup)$tmpfile" "$3"
    fi
}

merge_file () {
    MERGED="$1"

    f=$(git ls-files -u -- "$MERGED")
    if test -z "$f" ; then
	if test ! -f "$MERGED" ; then
	    echo "$MERGED: file not found"
	else
	    echo "$MERGED: file does not need merging"
	fi
	return 1
    fi

    ext="$$$(expr "$MERGED" : '.*\(\.[^/]*\)$')"
    BACKUP="./$MERGED.BACKUP.$ext"
    LOCAL="./$MERGED.LOCAL.$ext"
    REMOTE="./$MERGED.REMOTE.$ext"
    BASE="./$MERGED.BASE.$ext"

    base_mode=$(git ls-files -u -- "$MERGED" | awk '{if ($3==1) print $1;}')
    local_mode=$(git ls-files -u -- "$MERGED" | awk '{if ($3==2) print $1;}')
    remote_mode=$(git ls-files -u -- "$MERGED" | awk '{if ($3==3) print $1;}')

    if is_submodule "$local_mode" || is_submodule "$remote_mode"; then
	echo "Submodule merge conflict for '$MERGED':"
	local_sha1=$(git ls-files -u -- "$MERGED" | awk '{if ($3==2) print $2;}')
	remote_sha1=$(git ls-files -u -- "$MERGED" | awk '{if ($3==3) print $2;}')
	describe_file "$local_mode" "local" "$local_sha1"
	describe_file "$remote_mode" "remote" "$remote_sha1"
	resolve_submodule_merge
	return
    fi

    mv -- "$MERGED" "$BACKUP"
    cp -- "$BACKUP" "$MERGED"

    base_present   && checkout_staged_file 1 "$MERGED" "$BASE"
    local_present  && checkout_staged_file 2 "$MERGED" "$LOCAL"
    remote_present && checkout_staged_file 3 "$MERGED" "$REMOTE"

    if test -z "$local_mode" -o -z "$remote_mode"; then
	echo "Deleted merge conflict for '$MERGED':"
	describe_file "$local_mode" "local" "$LOCAL"
	describe_file "$remote_mode" "remote" "$REMOTE"
	resolve_deleted_merge
	return
    fi

    if is_symlink "$local_mode" || is_symlink "$remote_mode"; then
	echo "Symbolic link merge conflict for '$MERGED':"
	describe_file "$local_mode" "local" "$LOCAL"
	describe_file "$remote_mode" "remote" "$REMOTE"
	resolve_symlink_merge
	return
    fi

    echo "Normal merge conflict for '$MERGED':"
    describe_file "$local_mode" "local" "$LOCAL"
    describe_file "$remote_mode" "remote" "$REMOTE"
    if "$prompt" = true; then
	printf "Hit return to start merge resolution tool (%s): " "$merge_tool"
	read ans || return 1
    fi

    if base_present; then
	    present=true
    else
	    present=false
    fi

    if ! run_merge_tool "$merge_tool" "$present"; then
	echo "merge of $MERGED failed" 1>&2
	mv -- "$BACKUP" "$MERGED"

	if test "$merge_keep_temporaries" = "false"; then
	    cleanup_temp_files
	fi

	return 1
    fi

    if test "$merge_keep_backup" = "true"; then
	mv -- "$BACKUP" "$MERGED.orig"
    else
	rm -- "$BACKUP"
    fi

    git add -- "$MERGED"
    cleanup_temp_files
    return 0
}

prompt=$(git config --bool mergetool.prompt || echo true)

while test $# != 0
do
    case "$1" in
	-t|--tool*)
	    case "$#,$1" in
		*,*=*)
		    merge_tool=$(expr "z$1" : 'z-[^=]*=\(.*\)')
		    ;;
		1,*)
		    usage ;;
		*)
		    merge_tool="$2"
		    shift ;;
	    esac
	    ;;
	-y|--no-prompt)
	    prompt=false
	    ;;
	--prompt)
	    prompt=true
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

prompt_after_failed_merge() {
    while true; do
	printf "Continue merging other unresolved paths (y/n) ? "
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

if test -z "$merge_tool"; then
    merge_tool=$(get_merge_tool "$merge_tool") || exit
fi
merge_keep_backup="$(git config --bool mergetool.keepBackup || echo true)"
merge_keep_temporaries="$(git config --bool mergetool.keepTemporaries || echo false)"

last_status=0
rollup_status=0
rerere=false

files_to_merge() {
    if test "$rerere" = true
    then
	git rerere remaining
    else
	git ls-files -u | sed -e 's/^[^	]*	//' | sort -u
    fi
}


if test $# -eq 0 ; then
    cd_to_toplevel

    if test -e "$GIT_DIR/MERGE_RR"
    then
	rerere=true
    fi

    files=$(files_to_merge)
    if test -z "$files" ; then
	echo "No files need merging"
	exit 0
    fi

    # Save original stdin
    exec 3<&0

    printf "Merging:\n"
    printf "$files\n"

    files_to_merge |
    while IFS= read i
    do
	if test $last_status -ne 0; then
	    prompt_after_failed_merge <&3 || exit 1
	fi
	printf "\n"
	merge_file "$i" <&3
	last_status=$?
	if test $last_status -ne 0; then
	    rollup_status=1
	fi
    done
else
    while test $# -gt 0; do
	if test $last_status -ne 0; then
	    prompt_after_failed_merge || exit 1
	fi
	printf "\n"
	merge_file "$1"
	last_status=$?
	if test $last_status -ne 0; then
	    rollup_status=1
	fi
	shift
    done
fi

exit $rollup_status
