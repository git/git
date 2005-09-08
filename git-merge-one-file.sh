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
#   $5 - orignal file mode (or empty)
#   $6 - file in branch1 mode (or empty)
#   $7 - file in branch2 mode (or empty)
#
# Handle some trivial cases.. The _really_ trivial cases have
# been handled already by git-read-tree, but that one doesn't
# do any merges that might change the tree layout.

case "${1:-.}${2:-.}${3:-.}" in
#
# Deleted in both or deleted in one and unchanged in the other
#
"$1.." | "$1.$1" | "$1$1.")
	if [ "$2" ]; then
		echo "Removing $4"
	fi
	if test -f "$4"; then
		rm -f -- "$4"
	fi &&
		exec git-update-index --remove -- "$4"
	;;

#
# Added in one.
#
".$2." | "..$3" )
	echo "Adding $4"
	git-update-index --add --cacheinfo "$6$7" "$2$3" "$4" &&
		exec git-checkout-index -u -f -- "$4"
	;;

#
# Added in both (check for same permissions).
#
".$3$2")
	if [ "$6" != "$7" ]; then
		echo "ERROR: File $4 added identically in both branches,"
		echo "ERROR: but permissions conflict $6->$7."
		exit 1
	fi
	echo "Adding $4"
	git-update-index --add --cacheinfo "$6" "$2" "$4" &&
		exec git-checkout-index -u -f -- "$4"
	;;

#
# Modified in both, but differently.
#
"$1$2$3")
	echo "Auto-merging $4."
	orig=`git-unpack-file $1`
	src2=`git-unpack-file $3`

	# We reset the index to the first branch, making
	# git-diff-file useful
	git-update-index --add --cacheinfo "$6" "$2" "$4"
		git-checkout-index -u -f -- "$4" &&
		merge "$4" "$orig" "$src2"
	ret=$?
	rm -f -- "$orig" "$src2"

	if [ "$6" != "$7" ]; then
		echo "ERROR: Permissions conflict: $5->$6,$7."
		ret=1
	fi

	if [ $ret -ne 0 ]; then
		echo "ERROR: Merge conflict in $4."
		exit 1
	fi
	exec git-update-index -- "$4"
	;;

*)
	echo "ERROR: $4: Not handling case $1 -> $2 -> $3"
	;;
esac
exit 1
