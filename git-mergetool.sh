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
	mv -- "$BACKUP" "$MERGED.orig"
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
    else
	if base_present; then
	    echo "modified"
	else
	    echo "created"
	fi
    fi
}


resolve_symlink_merge () {
    while true; do
	printf "Use (l)ocal or (r)emote, or (a)bort? "
	read ans
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
	read ans
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

    mv -- "$MERGED" "$BACKUP"
    cp -- "$BACKUP" "$MERGED"

    base_mode=$(git ls-files -u -- "$MERGED" | awk '{if ($3==1) print $1;}')
    local_mode=$(git ls-files -u -- "$MERGED" | awk '{if ($3==2) print $1;}')
    remote_mode=$(git ls-files -u -- "$MERGED" | awk '{if ($3==3) print $1;}')

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
	read ans
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
	read ans
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

if test $# -eq 0 ; then
    files=$(git ls-files -u | sed -e 's/^[^	]*	//' | sort -u)
    if test -z "$files" ; then
	echo "No files need merging"
	exit 0
    fi
    echo Merging the files: "$files"
    git ls-files -u |
    sed -e 's/^[^	]*	//' |
    sort -u |
    while IFS= read i
    do
	if test $last_status -ne 0; then
	    prompt_after_failed_merge < /dev/tty || exit 1
	fi
	printf "\n"
	merge_file "$i" < /dev/tty > /dev/tty
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
