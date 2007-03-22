#!/bin/sh
#
# This program resolves merge conflicts in git
#
# Copyright (c) 2006 Theodore Y. Ts'o
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Junio C Hammano.
#

USAGE='[--tool=tool] [file to merge] ...'
SUBDIRECTORY_OK=Yes
. git-sh-setup
require_work_tree

# Returns true if the mode reflects a symlink
function is_symlink () {
    test "$1" = 120000
}

function local_present () {
    test -n "$local_mode"
}

function remote_present () {
    test -n "$remote_mode"
}

function base_present () {
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

function describe_file () {
    mode="$1"
    branch="$2"
    file="$3"

    echo -n "    "
    if test -z "$mode"; then
	echo -n "'$path' was deleted"
    elif is_symlink "$mode" ; then
	echo -n "'$path' is a symlink containing '"
	cat "$file"
	echo -n "'"
    else
	if base_present; then
	    echo -n "'$path' was created"
	else
	    echo -n "'$path' was modified"
	fi
    fi
    echo " in the $branch branch"
}


resolve_symlink_merge () {
    while /bin/true; do
	echo -n "Use (r)emote or (l)ocal, or (a)bort? "
	read ans
	case "$ans" in
	    [lL]*)
		git-checkout-index -f --stage=2 -- "$path"
		git-add -- "$path"
		cleanup_temp_files --save-backup
		return
		;;
	   [rR]*)
		git-checkout-index -f --stage=3 -- "$path"
		git-add -- "$path"
		cleanup_temp_files --save-backup
		return
		;;
	    [qQ]*)
		exit 1
		;;
	    esac
	done
}

resolve_deleted_merge () {
    while /bin/true; do
	echo -n "Use (m)odified or (d)eleted file, or (a)bort? "
	read ans
	case "$ans" in
	    [mM]*)
		git-add -- "$path"
		cleanup_temp_files --save-backup
		return
		;;
	   [dD]*)
		git-rm -- "$path"
		cleanup_temp_files
		return
		;;
	    [qQ]*)
		exit 1
		;;
	    esac
	done
}

merge_file () {
    path="$1"

    if test ! -f "$path" ; then
	echo "$path: file not found"
	exit 1
    fi

    f=`git-ls-files -u -- "$path"`
    if test -z "$f" ; then
	echo "$path: file does not need merging"
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

    base_present   && git cat-file blob ":1:$path" > "$BASE" 2>/dev/null
    local_present  && git cat-file blob ":2:$path" > "$LOCAL" 2>/dev/null
    remote_present && git cat-file blob ":3:$path" > "$REMOTE" 2>/dev/null

    if test -z "$local_mode" -o -z "$remote_mode"; then
	echo "Deleted merge conflict for $path:"
	describe_file "$local_mode" "local" "$LOCAL"
	describe_file "$remote_mode" "remote" "$REMOTE"
	resolve_deleted_merge
	return
    fi

    if is_symlink "$local_mode" || is_symlink "$remote_mode"; then
	echo "Symlink merge conflict for $path:"
	describe_file "$local_mode" "local" "$LOCAL"
	describe_file "$remote_mode" "remote" "$REMOTE"
	resolve_symlink_merge
	return
    fi

    echo "Normal merge conflict for $path:"
    describe_file "$local_mode" "local" "$LOCAL"
    describe_file "$remote_mode" "remote" "$REMOTE"
    echo -n "Hit return to start merge resolution tool ($merge_tool): "
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
	    if test "$status" -eq 0; then
		rm "$BACKUP"
	    fi
	    ;;
	tkdiff)
	    if base_present ; then
		tkdiff -a "$BASE" -o "$path" -- "$LOCAL" "$REMOTE"
	    else
		tkdiff -o "$path" -- "$LOCAL" "$REMOTE"
	    fi
	    status=$?
	    if test "$status" -eq 0; then
		mv -- "$BACKUP" "$path.orig"
	    fi
	    ;;
	meld)
	    touch "$BACKUP"
	    meld -- "$LOCAL" "$path" "$REMOTE"
	    if test "$path" -nt "$BACKUP" ; then
		status=0;
	    else
		while true; do
		    echo "$path seems unchanged."
		    echo -n "Was the merge successful? [y/n] "
		    read answer < /dev/tty
		    case "$answer" in
			y*|Y*) status=0; break ;;
			n*|N*) status=1; break ;;
		    esac
		done
	    fi
	    if test "$status" -eq 0; then
		mv -- "$BACKUP" "$path.orig"
	    fi
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
	    if test "$path" -nt "$BACKUP" ; then
		status=0;
	    else
		while true; do
		    echo "$path seems unchanged."
		    echo -n "Was the merge successful? [y/n] "
		    read answer < /dev/tty
		    case "$answer" in
			y*|Y*) status=0; break ;;
			n*|N*) status=1; break ;;
		    esac
		done
	    fi
	    if test "$status" -eq 0; then
		mv -- "$BACKUP" "$path.orig"
	    fi
	    ;;
	emerge)
	    if base_present ; then
		emacs -f emerge-files-with-ancestor-command "$LOCAL" "$REMOTE" "$BASE" "$path"
	    else
		emacs -f emerge-files-command "$LOCAL" "$REMOTE" "$path"
	    fi
	    status=$?
	    if test "$status" -eq 0; then
		mv -- "$BACKUP" "$path.orig"
	    fi
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

while case $# in 0) break ;; esac
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
    merge_tool=`git-config merge.tool`
    if test $merge_tool = kdiff3 -o $merge_tool = tkdiff -o \
	$merge_tool = xxdiff -o $merge_tool = meld ; then
	unset merge_tool
    fi
fi

if test -z "$merge_tool" ; then
    if type kdiff3 >/dev/null 2>&1 && test -n "$DISPLAY"; then
	merge_tool="kdiff3";
    elif type tkdiff >/dev/null 2>&1 && test -n "$DISPLAY"; then
	merge_tool=tkdiff
    elif type xxdiff >/dev/null 2>&1 && test -n "$DISPLAY"; then
	merge_tool=xxdiff
    elif type meld >/dev/null 2>&1 && test -n "$DISPLAY"; then
	merge_tool=meld
    elif type emacs >/dev/null 2>&1; then
	merge_tool=emerge
    else
	echo "No available merge resolution programs available."
	exit 1
    fi
fi

case "$merge_tool" in
    kdiff3|tkdiff|meld|xxdiff)
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
		echo ""
		merge_file "$i" < /dev/tty > /dev/tty
	done
else
	while test $# -gt 0; do
		echo ""
		merge_file "$1"
		shift
	done
fi
exit 0
