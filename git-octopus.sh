#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#
# Resolve two or more trees recorded in $GIT_DIR/FETCH_HEAD.
#
. git-sh-setup

usage () {
    die "usage: git octopus"
}

# Sanity check the heads early.
while read SHA1 REPO
do
	test $(git-cat-file -t $SHA1) = "commit" ||
		die "$REPO given to octopus is not a commit"
done <"$GIT_DIR/FETCH_HEAD"

head=$(git-rev-parse --verify HEAD) || exit

git-update-index --refresh ||
	die "Your working tree is dirty."
test "$(git-diff-index --cached "$head")" = "" ||
	die "Your working tree does not match HEAD."

# MRC is the current "merge reference commit"
# MRT is the current "merge result tree"

MRC=$head PARENT="-p $head"
MRT=$(git-write-tree)
CNT=1 ;# counting our head
NON_FF_MERGE=0
while read SHA1 REPO
do
	common=$(git-merge-base $MRC $SHA1) ||
		die "Unable to find common commit with $SHA1 from $REPO"

	if test "$common" = $SHA1
	then
		echo "Already up-to-date: $REPO"
		continue
	fi

	CNT=`expr $CNT + 1`
	PARENT="$PARENT -p $SHA1"

	if test "$common,$NON_FF_MERGE" = "$MRC,0"
	then
		# The first head being merged was a fast-forward.
		# Advance MRC to the head being merged, and use that
		# tree as the intermediate result of the merge.
		# We still need to count this as part of the parent set.

		echo "Fast forwarding to: $REPO"
		git-read-tree -u -m $head $SHA1 || exit
		MRC=$SHA1 MRT=$(git-write-tree)
		continue
	fi

	NON_FF_MERGE=1

	echo "Trying simple merge with $REPO"
	git-read-tree -u -m $common $MRT $SHA1 || exit
	next=$(git-write-tree 2>/dev/null)
	if test $? -ne 0
	then
		echo "Simple merge did not work, trying automatic merge."
		git-merge-index -o git-merge-one-file -a || {
		git-read-tree --reset "$head"
		git-checkout-index -f -q -u -a
		die "Automatic merge failed; should not be doing Octopus"
		}
		next=$(git-write-tree 2>/dev/null)
	fi
	MRC=$common
	MRT=$next
done <"$GIT_DIR/FETCH_HEAD"

# Just to be careful in case the user feeds nonsense to us.
case "$CNT" in
1)
	echo "No changes."
	exit 0 ;;
esac
result_commit=$(git-fmt-merge-msg <"$GIT_DIR/FETCH_HEAD" |
		git-commit-tree $MRT $PARENT)
echo "Committed merge $result_commit"
git-update-ref HEAD $result_commit $head
git-diff-tree -p $head $result_commit | git-apply --stat
