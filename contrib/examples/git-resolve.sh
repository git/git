#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#
# Resolve two trees.
#

echo 'WARNING: This command is DEPRECATED and will be removed very soon.' >&2
echo 'WARNING: Please use git-merge or git-pull instead.' >&2
sleep 2

USAGE='<head> <remote> <merge-message>'
. git-sh-setup

dropheads() {
	rm -f -- "$GIT_DIR/MERGE_HEAD" \
		"$GIT_DIR/LAST_MERGE" || exit 1
}

head=$(git-rev-parse --verify "$1"^0) &&
merge=$(git-rev-parse --verify "$2"^0) &&
merge_name="$2" &&
merge_msg="$3" || usage

#
# The remote name is just used for the message,
# but we do want it.
#
if [ -z "$head" -o -z "$merge" -o -z "$merge_msg" ]; then
	usage
fi

dropheads
echo $head > "$GIT_DIR"/ORIG_HEAD
echo $merge > "$GIT_DIR"/LAST_MERGE

common=$(git-merge-base $head $merge)
if [ -z "$common" ]; then
	die "Unable to find common commit between" $merge $head
fi

case "$common" in
"$merge")
	echo "Already up-to-date. Yeeah!"
	dropheads
	exit 0
	;;
"$head")
	echo "Updating $(git-rev-parse --short $head)..$(git-rev-parse --short $merge)"
	git-read-tree -u -m $head $merge || exit 1
	git-update-ref -m "resolve $merge_name: Fast forward" \
		HEAD "$merge" "$head"
	git-diff-tree -p $head $merge | git-apply --stat
	dropheads
	exit 0
	;;
esac

# We are going to make a new commit.
git var GIT_COMMITTER_IDENT >/dev/null || exit

# Find an optimum merge base if there are more than one candidates.
LF='
'
common=$(git-merge-base -a $head $merge)
case "$common" in
?*"$LF"?*)
	echo "Trying to find the optimum merge base."
	G=.tmp-index$$
	best=
	best_cnt=-1
	for c in $common
	do
		rm -f $G
		GIT_INDEX_FILE=$G git-read-tree -m $c $head $merge \
			2>/dev/null || continue
		# Count the paths that are unmerged.
		cnt=`GIT_INDEX_FILE=$G git-ls-files --unmerged | wc -l`
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
esac

echo "Trying to merge $merge into $head using $common."
git-update-index --refresh 2>/dev/null
git-read-tree -u -m $common $head $merge || exit 1
result_tree=$(git-write-tree  2> /dev/null)
if [ $? -ne 0 ]; then
	echo "Simple merge failed, trying Automatic merge"
	git-merge-index -o git-merge-one-file -a
	if [ $? -ne 0 ]; then
		echo $merge > "$GIT_DIR"/MERGE_HEAD
		die "Automatic merge failed, fix up by hand"
	fi
	result_tree=$(git-write-tree) || exit 1
fi
result_commit=$(echo "$merge_msg" | git-commit-tree $result_tree -p $head -p $merge)
echo "Committed merge $result_commit"
git-update-ref -m "resolve $merge_name: In-index merge" \
	HEAD "$result_commit" "$head"
git-diff-tree -p $head $result_commit | git-apply --stat
dropheads
