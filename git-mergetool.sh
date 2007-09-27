#!/bin/sh
#
# This program resolves merge conflicts in git
#
# Copyright (c) 2006 Theodore Y. Ts'o
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Junio C Hamano.
#

USAGE='[--tool=tool] [file to merge] ...'
SUBDIRECTORY_OK=Yes
. git-sh-setup
require_work_tree
prefix=$(git rev-parse --show-prefix)

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
	mv -- "$BACKUP" "$path.orig"
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
		git checkout-index -f --stage=2 -- "$path"
		git add -- "$path"
		cleanup_temp_files --save-backup
		return
		;;
	    [rR]*)
		git checkout-index -f --stage=3 -- "$path"
		git add -- "$path"
		cleanup_temp_files --save-backup
		return
		;;
	    [aA]*)
		exit 1
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
		git add -- "$path"
		cleanup_temp_files --save-backup
		return
		;;
	    [dD]*)
		git rm -- "$path" > /dev/null
		cleanup_temp_files
		return
		;;
	    [aA]*)
		exit 1
		;;
	    esac
	done
}

check_unchanged () {
    if test "$path" -nt "$BACKUP" ; then
	status=0;
    else
	while true; do
	    echo "$path seems unchanged."
	    printf "Was the merge successful? [y/n] "
	    read answer < /dev/tty
	    case "$answer" in
		y*|Y*) status=0; break ;;
		n*|N*) status=1; break ;;
	    esac
	done
    fi
}

save_backup () {
    if test "$status" -eq 0; then
	mv -- "$BACKUP" "$path.orig"
    fi
}

remove_backup () {
    if test "$status" -eq 0; then
	rm "$BACKUP"
    fi
}

merge_file () {
    path="$1"

    f=`git ls-files -u -- "$path"`
    if test -z "$f" ; then
	if test ! -f "$path" ; then
	    echo "$path: file not found"
	else
	    echo "$path: file does not need merging"
	fi
	exit 1
    fi

    BACKUP="$path.BACKUP.$$"
    LOCAL="$path.LOCAL.$$"
    REMOTE="$path.REMOTE.$$"
    BASE="$path.BASE.$$"

    mv -- "$path" "$BACKUP"
    cp -- "$BACKUP" "$path"

    base_mode=`git ls-files -u -- "$path" | awk '{if ($3==1) print $1;}'`
    local_mode=`git ls-files -u -- "$path" | awk '{if ($3==2) print $1;}'`
    remote_mode=`git ls-files -u -- "$path" | awk '{if ($3==3) print $1;}'`

    base_present   && git cat-file blob ":1:$prefix$path" >"$BASE" 2>/dev/null
    local_present  && git cat-file blob ":2:$prefix$path" >"$LOCAL" 2>/dev/null
    remote_present && git cat-file blob ":3:$prefix$path" >"$REMOTE" 2>/dev/null

    if test -z "$local_mode" -o -z "$remote_mode"; then
	echo "Deleted merge conflict for '$path':"
	describe_file "$local_mode" "local" "$LOCAL"
	describe_file "$remote_mode" "remote" "$REMOTE"
	resolve_deleted_merge
	return
    fi

    if is_symlink "$local_mode" || is_symlink "$remote_mode"; then
	echo "Symbolic link merge conflict for '$path':"
	describe_file "$local_mode" "local" "$LOCAL"
	describe_file "$remote_mode" "remote" "$REMOTE"
	resolve_symlink_merge
	return
    fi

    echo "Normal merge conflict for '$path':"
    describe_file "$local_mode" "local" "$LOCAL"
    describe_file "$remote_mode" "remote" "$REMOTE"
    printf "Hit return to start merge resolution tool (%s): " "$merge_tool"
    read ans

    case "$merge_tool" in
	kdiff3)
	    if base_present ; then
		(kdiff3 --auto --L1 "$path (Base)" -L2 "$path (Local)" --L3 "$path (Remote)" \
		    -o "$path" -- "$BASE" "$LOCAL" "$REMOTE" > /dev/null 2>&1)
	    else
		(kdiff3 --auto -L1 "$path (Local)" --L2 "$path (Remote)" \
		    -o "$path" -- "$LOCAL" "$REMOTE" > /dev/null 2>&1)
	    fi
	    status=$?
	    remove_backup
	    ;;
	tkdiff)
	    if base_present ; then
		tkdiff -a "$BASE" -o "$path" -- "$LOCAL" "$REMOTE"
	    else
		tkdiff -o "$path" -- "$LOCAL" "$REMOTE"
	    fi
	    status=$?
	    save_backup
	    ;;
	meld|vimdiff)
	    touch "$BACKUP"
	    $merge_tool -- "$LOCAL" "$path" "$REMOTE"
	    check_unchanged
	    save_backup
	    ;;
	gvimdiff)
		touch "$BACKUP"
		gvimdiff -f -- "$LOCAL" "$path" "$REMOTE"
		check_unchanged
		save_backup
		;;
	xxdiff)
	    touch "$BACKUP"
	    if base_present ; then
		xxdiff -X --show-merged-pane \
		    -R 'Accel.SaveAsMerged: "Ctrl-S"' \
		    -R 'Accel.Search: "Ctrl+F"' \
		    -R 'Accel.SearchForward: "Ctrl-G"' \
		    --merged-file "$path" -- "$LOCAL" "$BASE" "$REMOTE"
	    else
		xxdiff -X --show-merged-pane \
		    -R 'Accel.SaveAsMerged: "Ctrl-S"' \
		    -R 'Accel.Search: "Ctrl+F"' \
		    -R 'Accel.SearchForward: "Ctrl-G"' \
		    --merged-file "$path" -- "$LOCAL" "$REMOTE"
	    fi
	    check_unchanged
	    save_backup
	    ;;
	opendiff)
	    touch "$BACKUP"
	    if base_present; then
		opendiff "$LOCAL" "$REMOTE" -ancestor "$BASE" -merge "$path" | cat
	    else
		opendiff "$LOCAL" "$REMOTE" -merge "$path" | cat
	    fi
	    check_unchanged
	    save_backup
	    ;;
	emerge)
	    if base_present ; then
		emacs -f emerge-files-with-ancestor-command "$LOCAL" "$REMOTE" "$BASE" "$path"
	    else
		emacs -f emerge-files-command "$LOCAL" "$REMOTE" "$path"
	    fi
	    status=$?
	    save_backup
	    ;;
    esac
    if test "$status" -ne 0; then
	echo "merge of $path failed" 1>&2
	mv -- "$BACKUP" "$path"
	exit 1
    fi
    git add -- "$path"
    cleanup_temp_files
}

while test $# != 0
do
    case "$1" in
	-t|--tool*)
	    case "$#,$1" in
		*,*=*)
		    merge_tool=`expr "z$1" : 'z-[^=]*=\(.*\)'`
		    ;;
		1,*)
		    usage ;;
		*)
		    merge_tool="$2"
		    shift ;;
	    esac
	    ;;
	--)
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

if test -z "$merge_tool"; then
    merge_tool=`git config merge.tool`
    case "$merge_tool" in
	kdiff3 | tkdiff | xxdiff | meld | opendiff | emerge | vimdiff | gvimdiff | "")
	    ;; # happy
	*)
	    echo >&2 "git config option merge.tool set to unknown tool: $merge_tool"
	    echo >&2 "Resetting to default..."
	    unset merge_tool
	    ;;
    esac
fi

if test -z "$merge_tool" ; then
    if test -n "$DISPLAY"; then
        merge_tool_candidates="kdiff3 tkdiff xxdiff meld gvimdiff"
        if test -n "$GNOME_DESKTOP_SESSION_ID" ; then
            merge_tool_candidates="meld $merge_tool_candidates"
        fi
        if test "$KDE_FULL_SESSION" = "true"; then
            merge_tool_candidates="kdiff3 $merge_tool_candidates"
        fi
    fi
    if echo "${VISUAL:-$EDITOR}" | grep 'emacs' > /dev/null 2>&1; then
        merge_tool_candidates="$merge_tool_candidates emerge"
    fi
    if echo "${VISUAL:-$EDITOR}" | grep 'vim' > /dev/null 2>&1; then
        merge_tool_candidates="$merge_tool_candidates vimdiff"
    fi
    merge_tool_candidates="$merge_tool_candidates opendiff emerge vimdiff"
    echo "merge tool candidates: $merge_tool_candidates"
    for i in $merge_tool_candidates; do
        if test $i = emerge ; then
            cmd=emacs
        else
            cmd=$i
        fi
        if type $cmd > /dev/null 2>&1; then
            merge_tool=$i
            break
        fi
    done
    if test -z "$merge_tool" ; then
	echo "No available merge resolution programs available."
	exit 1
    fi
fi

case "$merge_tool" in
    kdiff3|tkdiff|meld|xxdiff|vimdiff|gvimdiff|opendiff)
	if ! type "$merge_tool" > /dev/null 2>&1; then
	    echo "The merge tool $merge_tool is not available"
	    exit 1
	fi
	;;
    emerge)
	if ! type "emacs" > /dev/null 2>&1; then
	    echo "Emacs is not available"
	    exit 1
	fi
	;;
    *)
	echo "Unknown merge tool: $merge_tool"
	exit 1
	;;
esac

if test $# -eq 0 ; then
	files=`git ls-files -u | sed -e 's/^[^	]*	//' | sort -u`
	if test -z "$files" ; then
		echo "No files need merging"
		exit 0
	fi
	echo Merging the files: $files
	git ls-files -u | sed -e 's/^[^	]*	//' | sort -u | while read i
	do
		printf "\n"
		merge_file "$i" < /dev/tty > /dev/tty
	done
else
	while test $# -gt 0; do
		printf "\n"
		merge_file "$1"
		shift
	done
fi
exit 0
