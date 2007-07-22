#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#
# Resolve two trees, 'stupid merge'.

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

# Give up if we are given more than two remotes -- not handling octopus.
case "$remotes" in
?*' '?*)
	exit 2 ;;
esac

# Find an optimum merge base if there are more than one candidates.
case "$bases" in
?*' '?*)
	echo "Trying to find the optimum merge base."
	G=.tmp-index$$
	best=
	best_cnt=-1
	for c in $bases
	do
		rm -f $G
		GIT_INDEX_FILE=$G git read-tree -m $c $head $remotes \
			 2>/dev/null ||	continue
		# Count the paths that are unmerged.
		cnt=`GIT_INDEX_FILE=$G git ls-files --unmerged | wc -l`
		if test $best_cnt -le 0 -o $cnt -le $best_cnt
		then
			best=$c
			best_cnt=$cnt
			if test "$best_cnt" -eq 0
			then
				# Cannot do any better than all trivial merge.
				break
			fi
		fi
	done
	rm -f $G
	common="$best"
	;;
*)
	common="$bases"
	;;
esac

git update-index --refresh 2>/dev/null
git read-tree -u -m $common $head $remotes || exit 2
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
