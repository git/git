#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#
# Fetch one or more remote refs and merge it/them into the current HEAD.

. git-sh-setup || die "Not a git archive"

orig_head=$(cat "$GIT_DIR/HEAD") || die "Pulling into a black hole?"
git-fetch --update-head-ok "$@" || exit 1

curr_head=$(cat "$GIT_DIR/HEAD")
if test "$curr_head" != "$orig_head"
then
	# The fetch involved updating the current branch.

	# The working tree and the index file is still based on the
	# $orig_head commit, but we are merging into $curr_head.
	# First update the working tree to match $curr_head.

	echo >&2 "Warning: fetch updated the current branch head."
	echo >&2 "Warning: fast forwarding your working tree."
	git-read-tree -u -m "$orig_head" "$curr_head" ||
		die "You need to first update your working tree."
fi

merge_head=$(sed -e 's/	.*//' "$GIT_DIR"/FETCH_HEAD | tr '\012' ' ')

case "$merge_head" in
'')
	echo >&2 "No changes."
	exit 0
	;;
*' '?*)
	echo >&2 "Pulling more than one heads; making an Octopus."
	exec git-octopus
	;;
esac

merge_name=$(git-fmt-merge-msg <"$GIT_DIR/FETCH_HEAD")
git-resolve "$(cat "$GIT_DIR"/HEAD)" $merge_head "$merge_name"
