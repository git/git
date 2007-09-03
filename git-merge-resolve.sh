#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
# Copyright (c) 2005 Junio C Hamano
#
# Resolve two trees, using enhanced multi-base read-tree.

# The first parameters up to -- are merge bases; the rest are heads.
bases= head= remotes= sep_seen=
for arg
do
	case ",$sep_seen,$head,$arg," in
	*,--,)
		sep_seen=yes
		;;
	,yes,,*)
		head=$arg
		;;
	,yes,*)
		remotes="$remotes$arg "
		;;
	*)
		bases="$bases$arg "
		;;
	esac
done

# Give up if we are given two or more remotes -- not handling octopus.
case "$remotes" in
?*' '?*)
	exit 2 ;;
esac

# Give up if this is a baseless merge.
if test '' = "$bases"
then
	exit 2
fi

git update-index --refresh 2>/dev/null
git read-tree -u -m --aggressive $bases $head $remotes || exit 2
echo "Trying simple merge."
if result_tree=$(git write-tree  2>/dev/null)
then
	exit 0
else
	echo "Simple merge failed, trying Automatic merge."
	if git-merge-index -o git-merge-one-file -a
	then
		exit 0
	else
		exit 1
	fi
fi
