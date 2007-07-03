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

case "${1:-.}${2:-.}${3:-.}" in
#
# Deleted in both or deleted in one and unchanged in the other
#
"$1.." | "$1.$1" | "$1$1.")
	if [ "$2" ]; then
		echo "Removing $4"
	else
		# read-tree checked that index matches HEAD already,
		# so we know we do not have this path tracked.
		# there may be an unrelated working tree file here,
		# which we should just leave unmolested.
		exit 0
	fi
	if test -f "$4"; then
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
	# to be done.
	;;
"..$3")
	echo "Adding $4"
	test -f "$4" || {
		echo "ERROR: untracked $4 is overwritten by the merge."
		exit 1
	}
	git update-index --add --cacheinfo "$6$7" "$2$3" "$4" &&
		exec git checkout-index -u -f -- "$4"
	;;

#
# Added in both, identically (check for same permissions).
#
".$3$2")
	if [ "$6" != "$7" ]; then
		echo "ERROR: File $4 added identically in both branches,"
		echo "ERROR: but permissions conflict $6->$7."
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
		echo "ERROR: $4: Not merging symbolic link changes."
		exit 1
		;;
	esac

	src2=`git-unpack-file $3`
	case "$1" in
	'')
		echo "Added $4 in both, but differently."
		# This extracts OUR file in $orig, and uses git apply to
		# remove lines that are unique to ours.
		orig=`git-unpack-file $2`
		sz0=`wc -c <"$orig"`
		diff -u -La/$orig -Lb/$orig $orig $src2 | git apply --no-add
		sz1=`wc -c <"$orig"`

		# If we do not have enough common material, it is not
		# worth trying two-file merge using common subsections.
		expr "$sz0" \< "$sz1" \* 2 >/dev/null || : >$orig
		;;
	*)
		echo "Auto-merging $4"
		orig=`git-unpack-file $1`
		;;
	esac

	# Be careful for funny filename such as "-L" in "$4", which
	# would confuse "merge" greatly.
	src1=`git-unpack-file $2`
	git merge-file "$src1" "$orig" "$src2"
	ret=$?

	# Create the working tree file, using "our tree" version from the
	# index, and then store the result of the merge.
	git checkout-index -f --stage=2 -- "$4" && cat "$src1" >"$4"
	rm -f -- "$orig" "$src1" "$src2"

	if [ "$6" != "$7" ]; then
		echo "ERROR: Permissions conflict: $5->$6,$7."
		ret=1
	fi
	if [ "$1" = '' ]; then
		ret=1
	fi

	if [ $ret -ne 0 ]; then
		echo "ERROR: Merge conflict in $4"
		exit 1
	fi
	exec git update-index -- "$4"
	;;

*)
	echo "ERROR: $4: Not handling case $1 -> $2 -> $3"
	;;
esac
exit 1
