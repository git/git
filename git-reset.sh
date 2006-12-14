#!/bin/sh

USAGE='[--mixed | --soft | --hard]  [<commit-ish>]'
SUBDIRECTORY_OK=Yes
. git-sh-setup

TOP=$(git-rev-parse --show-cdup)
if test ! -z "$TOP"
then
	cd "$TOP"
fi

update=
reset_type=--mixed
case "$1" in
--mixed | --soft | --hard)
	reset_type="$1"
	shift
	;;
-*)
        usage ;;
esac

case $# in
0) rev=HEAD ;;
1) rev=$(git-rev-parse --verify "$1") || exit ;;
*) usage ;;
esac
rev=$(git-rev-parse --verify $rev^0) || exit

# We need to remember the set of paths that _could_ be left
# behind before a hard reset, so that we can remove them.
if test "$reset_type" = "--hard"
then
	update=-u
fi

# Soft reset does not touch the index file nor the working tree
# at all, but requires them in a good order.  Other resets reset
# the index file to the tree object we are switching to.
if test "$reset_type" = "--soft"
then
	if test -f "$GIT_DIR/MERGE_HEAD" ||
	   test "" != "$(git-ls-files --unmerged)"
	then
		die "Cannot do a soft reset in the middle of a merge."
	fi
else
	git-read-tree --reset $update "$rev" || exit
fi

# Any resets update HEAD to the head being switched to.
if orig=$(git-rev-parse --verify HEAD 2>/dev/null)
then
	echo "$orig" >"$GIT_DIR/ORIG_HEAD"
else
	rm -f "$GIT_DIR/ORIG_HEAD"
fi
git-update-ref -m "reset $reset_type $*" HEAD "$rev"
update_ref_status=$?

case "$reset_type" in
--hard )
	;; # Nothing else to do
--soft )
	;; # Nothing else to do
--mixed )
	# Report what has not been updated.
	git-update-index --refresh
	;;
esac

rm -f "$GIT_DIR/MERGE_HEAD" "$GIT_DIR/rr-cache/MERGE_RR" \
	"$GIT_DIR/SQUASH_MSG" "$GIT_DIR/MERGE_MSG"

exit $update_ref_status
