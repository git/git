#!/bin/sh
#
# Copyright (c) 2005, 2006 Linus Torvalds and Junio C Hamano
#
USAGE='[--mixed | --soft | --hard]  [<commit-ish>] [ [--] <paths>...]'
SUBDIRECTORY_OK=Yes
. git-sh-setup
set_reflog_action "reset $*"
require_work_tree

update= reset_type=--mixed
unset rev

while case $# in 0) break ;; esac
do
	case "$1" in
	--mixed | --soft | --hard)
		reset_type="$1"
		;;
	--)
		break
		;;
	-*)
		usage
		;;
	*)
		rev=$(git rev-parse --verify "$1") || exit
		shift
		break
		;;
	esac
	shift
done

: ${rev=HEAD}
rev=$(git rev-parse --verify $rev^0) || exit

# Skip -- in "git reset HEAD -- foo" and "git reset -- foo".
case "$1" in --) shift ;; esac

# git reset --mixed tree [--] paths... can be used to
# load chosen paths from the tree into the index without
# affecting the working tree nor HEAD.
if test $# != 0
then
	test "$reset_type" = "--mixed" ||
		die "Cannot do partial $reset_type reset."

	git diff-index --cached $rev -- "$@" |
	sed -e 's/^:\([0-7][0-7]*\) [0-7][0-7]* \([0-9a-f][0-9a-f]*\) [0-9a-f][0-9a-f]* [A-Z]	\(.*\)$/\1 \2	\3/' |
	git update-index --add --remove --index-info || exit
	git update-index --refresh
	exit
fi

cd_to_toplevel

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
	   test "" != "$(git ls-files --unmerged)"
	then
		die "Cannot do a soft reset in the middle of a merge."
	fi
else
	git read-tree -v --reset $update "$rev" || exit
fi

# Any resets update HEAD to the head being switched to.
if orig=$(git rev-parse --verify HEAD 2>/dev/null)
then
	echo "$orig" >"$GIT_DIR/ORIG_HEAD"
else
	rm -f "$GIT_DIR/ORIG_HEAD"
fi
git update-ref -m "$GIT_REFLOG_ACTION" HEAD "$rev"
update_ref_status=$?

case "$reset_type" in
--hard )
	test $update_ref_status = 0 && {
		printf "HEAD is now at "
		GIT_PAGER= git log --max-count=1 --pretty=oneline \
			--abbrev-commit HEAD
	}
	;;
--soft )
	;; # Nothing else to do
--mixed )
	# Report what has not been updated.
	git update-index --refresh
	;;
esac

rm -f "$GIT_DIR/MERGE_HEAD" "$GIT_DIR/rr-cache/MERGE_RR" \
	"$GIT_DIR/SQUASH_MSG" "$GIT_DIR/MERGE_MSG"

exit $update_ref_status
