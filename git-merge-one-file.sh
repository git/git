#!/bin/sh
#
# Copyright (c) Linus Torvalds, 2005
#
# This is the git per-file merge script, called with
#
#   $1 - original file SHA1 (or empty)
#   $2 - file in branch1 SHA1 (or empty)
#   $3 - file in branch2 SHA1 (or empty)
#   $4 - pathname in repository
#   $5 - original file mode (or empty)
#   $6 - file in branch1 mode (or empty)
#   $7 - file in branch2 mode (or empty)
#
# Handle some trivial cases.. The _really_ trivial cases have
# been handled already by git read-tree, but that one doesn't
# do any merges that might change the tree layout.

USAGE='<orig blob> <our blob> <their blob> <path>'
USAGE="$USAGE <orig mode> <our mode> <their mode>"
LONG_USAGE="usage: git merge-one-file $USAGE

Blob ids and modes should be empty for missing files."

SUBDIRECTORY_OK=Yes
. git-sh-setup
cd_to_toplevel
require_work_tree

if test $# != 7
then
	echo "$LONG_USAGE"
	exit 1
fi

case "${1:-.}${2:-.}${3:-.}" in
#
# Deleted in both or deleted in one and unchanged in the other
#
"$1.." | "$1.$1" | "$1$1.")
	if test -n "$2"
	then
		echo "Removing $4"
	else
		# read-tree checked that index matches HEAD already,
		# so we know we do not have this path tracked.
		# there may be an unrelated working tree file here,
		# which we should just leave unmolested.  Make sure
		# we do not have it in the index, though.
		exec git update-index --remove -- "$4"
	fi
	if test -f "$4"
	then
		rm -f -- "$4" &&
		rmdir -p "$(expr "z$4" : 'z\(.*\)/')" 2>/dev/null || :
	fi &&
		exec git update-index --remove -- "$4"
	;;

#
# Added in one.
#
".$2.")
	# the other side did not add and we added so there is nothing
	# to be done, except making the path merged.
	exec git update-index --add --cacheinfo "$6" "$2" "$4"
	;;
"..$3")
	echo "Adding $4"
	if test -f "$4"
	then
		echo "ERROR: untracked $4 is overwritten by the merge." >&2
		exit 1
	fi
	git update-index --add --cacheinfo "$7" "$3" "$4" &&
		exec git checkout-index -u -f -- "$4"
	;;

#
# Added in both, identically (check for same permissions).
#
".$3$2")
	if test "$6" != "$7"
	then
		echo "ERROR: File $4 added identically in both branches," >&2
		echo "ERROR: but permissions conflict $6->$7." >&2
		exit 1
	fi
	echo "Adding $4"
	git update-index --add --cacheinfo "$6" "$2" "$4" &&
		exec git checkout-index -u -f -- "$4"
	;;

#
# Modified in both, but differently.
#
"$1$2$3" | ".$2$3")

	case ",$6,$7," in
	*,120000,*)
		echo "ERROR: $4: Not merging symbolic link changes." >&2
		exit 1
		;;
	*,160000,*)
		echo "ERROR: $4: Not merging conflicting submodule changes." >&2
		exit 1
		;;
	esac

	src1=$(git-unpack-file $2)
	src2=$(git-unpack-file $3)
	case "$1" in
	'')
		echo "Added $4 in both, but differently."
		orig=$(git-unpack-file $2)
		create_virtual_base "$orig" "$src2"
		;;
	*)
		echo "Auto-merging $4"
		orig=$(git-unpack-file $1)
		;;
	esac

	git merge-file "$src1" "$orig" "$src2"
	ret=$?
	msg=
	if test $ret != 0 || test -z "$1"
	then
		msg='content conflict'
		ret=1
	fi

	# Create the working tree file, using "our tree" version from the
	# index, and then store the result of the merge.
	git checkout-index -f --stage=2 -- "$4" && cat "$src1" >"$4" || exit 1
	rm -f -- "$orig" "$src1" "$src2"

	if test "$6" != "$7"
	then
		if test -n "$msg"
		then
			msg="$msg, "
		fi
		msg="${msg}permissions conflict: $5->$6,$7"
		ret=1
	fi

	if test $ret != 0
	then
		echo "ERROR: $msg in $4" >&2
		exit 1
	fi
	exec git update-index -- "$4"
	;;

*)
	echo "ERROR: $4: Not handling case $1 -> $2 -> $3" >&2
	;;
esac
exit 1
