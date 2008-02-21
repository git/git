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
OPTIONS_SPEC=
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
		return
		;;
	    [rR]*)
		git checkout-index -f --stage=3 -- "$MERGED"
		git add -- "$MERGED"
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
		git add -- "$MERGED"
		cleanup_temp_files --save-backup
		return
		;;
	    [dD]*)
		git rm -- "$MERGED" > /dev/null
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
    if test "$MERGED" -nt "$BACKUP" ; then
	status=0;
    else
	while true; do
	    echo "$MERGED seems unchanged."
	    printf "Was the merge successful? [y/n] "
	    read answer < /dev/tty
	    case "$answer" in
		y*|Y*) status=0; break ;;
		n*|N*) status=1; break ;;
	    esac
	done
    fi
}

merge_file () {
    MERGED="$1"

    f=`git ls-files -u -- "$MERGED"`
    if test -z "$f" ; then
	if test ! -f "$MERGED" ; then
	    echo "$MERGED: file not found"
	else
	    echo "$MERGED: file does not need merging"
	fi
	exit 1
    fi

    ext="$$$(expr "$MERGED" : '.*\(\.[^/]*\)$')"
    BACKUP="$MERGED.BACKUP.$ext"
    LOCAL="$MERGED.LOCAL.$ext"
    REMOTE="$MERGED.REMOTE.$ext"
    BASE="$MERGED.BASE.$ext"

    mv -- "$MERGED" "$BACKUP"
    cp -- "$BACKUP" "$MERGED"

    base_mode=`git ls-files -u -- "$MERGED" | awk '{if ($3==1) print $1;}'`
    local_mode=`git ls-files -u -- "$MERGED" | awk '{if ($3==2) print $1;}'`
    remote_mode=`git ls-files -u -- "$MERGED" | awk '{if ($3==3) print $1;}'`

    base_present   && git cat-file blob ":1:$prefix$MERGED" >"$BASE" 2>/dev/null
    local_present  && git cat-file blob ":2:$prefix$MERGED" >"$LOCAL" 2>/dev/null
    remote_present && git cat-file blob ":3:$prefix$MERGED" >"$REMOTE" 2>/dev/null

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
    printf "Hit return to start merge resolution tool (%s): " "$merge_tool"
    read ans

    case "$merge_tool" in
	kdiff3)
	    if base_present ; then
		("$merge_tool_path" --auto --L1 "$MERGED (Base)" --L2 "$MERGED (Local)" --L3 "$MERGED (Remote)" \
		    -o "$MERGED" -- "$BASE" "$LOCAL" "$REMOTE" > /dev/null 2>&1)
	    else
		("$merge_tool_path" --auto --L1 "$MERGED (Local)" --L2 "$MERGED (Remote)" \
		    -o "$MERGED" -- "$LOCAL" "$REMOTE" > /dev/null 2>&1)
	    fi
	    status=$?
	    ;;
	tkdiff)
	    if base_present ; then
		"$merge_tool_path" -a "$BASE" -o "$MERGED" -- "$LOCAL" "$REMOTE"
	    else
		"$merge_tool_path" -o "$MERGED" -- "$LOCAL" "$REMOTE"
	    fi
	    status=$?
	    ;;
	meld|vimdiff)
	    touch "$BACKUP"
	    "$merge_tool_path" -- "$LOCAL" "$MERGED" "$REMOTE"
	    check_unchanged
	    ;;
	gvimdiff)
	    touch "$BACKUP"
	    "$merge_tool_path" -f -- "$LOCAL" "$MERGED" "$REMOTE"
	    check_unchanged
	    ;;
	xxdiff)
	    touch "$BACKUP"
	    if base_present ; then
		"$merge_tool_path" -X --show-merged-pane \
		    -R 'Accel.SaveAsMerged: "Ctrl-S"' \
		    -R 'Accel.Search: "Ctrl+F"' \
		    -R 'Accel.SearchForward: "Ctrl-G"' \
		    --merged-file "$MERGED" -- "$LOCAL" "$BASE" "$REMOTE"
	    else
		"$merge_tool_path" -X --show-merged-pane \
		    -R 'Accel.SaveAsMerged: "Ctrl-S"' \
		    -R 'Accel.Search: "Ctrl+F"' \
		    -R 'Accel.SearchForward: "Ctrl-G"' \
		    --merged-file "$MERGED" -- "$LOCAL" "$REMOTE"
	    fi
	    check_unchanged
	    ;;
	opendiff)
	    touch "$BACKUP"
	    if base_present; then
		"$merge_tool_path" "$LOCAL" "$REMOTE" -ancestor "$BASE" -merge "$MERGED" | cat
	    else
		"$merge_tool_path" "$LOCAL" "$REMOTE" -merge "$MERGED" | cat
	    fi
	    check_unchanged
	    ;;
	ecmerge)
	    touch "$BACKUP"
	    if base_present; then
		"$merge_tool_path" "$BASE" "$LOCAL" "$REMOTE" --mode=merge3 --to="$MERGED"
	    else
		"$merge_tool_path" "$LOCAL" "$REMOTE" --mode=merge2 --to="$MERGED"
	    fi
	    check_unchanged
	    ;;
	emerge)
	    if base_present ; then
		"$merge_tool_path" -f emerge-files-with-ancestor-command "$LOCAL" "$REMOTE" "$BASE" "$(basename "$MERGED")"
	    else
		"$merge_tool_path" -f emerge-files-command "$LOCAL" "$REMOTE" "$(basename "$MERGED")"
	    fi
	    status=$?
	    ;;
    esac
    if test "$status" -ne 0; then
	echo "merge of $MERGED failed" 1>&2
	mv -- "$BACKUP" "$MERGED"
	exit 1
    fi

    if test "$merge_keep_backup" = "true"; then
	mv -- "$BACKUP" "$MERGED.orig"
    else
	rm -- "$BACKUP"
    fi

    git add -- "$MERGED"
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

valid_tool() {
	case "$1" in
		kdiff3 | tkdiff | xxdiff | meld | opendiff | emerge | vimdiff | gvimdiff | ecmerge)
			;; # happy
		*)
			return 1
			;;
	esac
}

init_merge_tool_path() {
	merge_tool_path=`git config mergetool.$1.path`
	if test -z "$merge_tool_path" ; then
		case "$1" in
			emerge)
				merge_tool_path=emacs
				;;
			*)
				merge_tool_path=$1
				;;
		esac
	fi
}


if test -z "$merge_tool"; then
    merge_tool=`git config merge.tool`
    if test -n "$merge_tool" && ! valid_tool "$merge_tool"; then
	    echo >&2 "git config option merge.tool set to unknown tool: $merge_tool"
	    echo >&2 "Resetting to default..."
	    unset merge_tool
    fi
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
        init_merge_tool_path $i
        if type "$merge_tool_path" > /dev/null 2>&1; then
            merge_tool=$i
            break
        fi
    done
    if test -z "$merge_tool" ; then
	echo "No known merge resolution program available."
	exit 1
    fi
else
    if ! valid_tool "$merge_tool"; then
        echo >&2 "Unknown merge_tool $merge_tool"
        exit 1
    fi

    init_merge_tool_path "$merge_tool"

    merge_keep_backup="$(git config --bool merge.keepBackup || echo true)"

    if ! type "$merge_tool_path" > /dev/null 2>&1; then
        echo "The merge tool $merge_tool is not available as '$merge_tool_path'"
        exit 1
    fi
fi


if test $# -eq 0 ; then
	files=`git ls-files -u | sed -e 's/^[^	]*	//' | sort -u`
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
