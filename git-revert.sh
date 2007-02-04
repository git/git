#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
# Copyright (c) 2005 Junio C Hamano
#

case "$0" in
*-revert* )
	test -t 0 && edit=-e
	replay=
	me=revert
	USAGE='[--edit | --no-edit] [-n] <commit-ish>' ;;
*-cherry-pick* )
	replay=t
	edit=
	me=cherry-pick
	USAGE='[--edit] [-n] [-r] [-x] <commit-ish>'  ;;
* )
	echo >&2 "What are you talking about?"
	exit 1 ;;
esac

SUBDIRECTORY_OK=Yes ;# we will cd up
. git-sh-setup
require_work_tree
cd_to_toplevel

no_commit=
while case "$#" in 0) break ;; esac
do
	case "$1" in
	-n|--n|--no|--no-|--no-c|--no-co|--no-com|--no-comm|\
	    --no-commi|--no-commit)
		no_commit=t
		;;
	-e|--e|--ed|--edi|--edit)
		edit=-e
		;;
	--n|--no|--no-|--no-e|--no-ed|--no-edi|--no-edit)
		edit=
		;;
	-r)
		: no-op ;;
	-x|--i-really-want-to-expose-my-private-commit-object-name)
		replay=
		;;
	-*)
		usage
		;;
	*)
		break
		;;
	esac
	shift
done

set_reflog_action "$me"

test "$me,$replay" = "revert,t" && usage

case "$no_commit" in
t)
	# We do not intend to commit immediately.  We just want to
	# merge the differences in.
	head=$(git-write-tree) ||
		die "Your index file is unmerged."
	;;
*)
	head=$(git-rev-parse --verify HEAD) ||
		die "You do not have a valid HEAD"
	files=$(git-diff-index --cached --name-only $head) || exit
	if [ "$files" ]; then
		die "Dirty index: cannot $me (dirty: $files)"
	fi
	;;
esac

rev=$(git-rev-parse --verify "$@") &&
commit=$(git-rev-parse --verify "$rev^0") ||
	die "Not a single commit $@"
prev=$(git-rev-parse --verify "$commit^1" 2>/dev/null) ||
	die "Cannot run $me a root commit"
git-rev-parse --verify "$commit^2" >/dev/null 2>&1 &&
	die "Cannot run $me a multi-parent commit."

encoding=$(git config i18n.commitencoding || echo UTF-8)

# "commit" is an existing commit.  We would want to apply
# the difference it introduces since its first parent "prev"
# on top of the current HEAD if we are cherry-pick.  Or the
# reverse of it if we are revert.

case "$me" in
revert)
	git show -s --pretty=oneline --encoding="$encoding" $commit |
	sed -e '
		s/^[^ ]* /Revert "/
		s/$/"/
	'
	echo
	echo "This reverts commit $commit."
	test "$rev" = "$commit" ||
	echo "(original 'git revert' arguments: $@)"
	base=$commit next=$prev
	;;

cherry-pick)
	pick_author_script='
	/^author /{
		s/'\''/'\''\\'\'\''/g
		h
		s/^author \([^<]*\) <[^>]*> .*$/\1/
		s/'\''/'\''\'\'\''/g
		s/.*/GIT_AUTHOR_NAME='\''&'\''/p

		g
		s/^author [^<]* <\([^>]*\)> .*$/\1/
		s/'\''/'\''\'\'\''/g
		s/.*/GIT_AUTHOR_EMAIL='\''&'\''/p

		g
		s/^author [^<]* <[^>]*> \(.*\)$/\1/
		s/'\''/'\''\'\'\''/g
		s/.*/GIT_AUTHOR_DATE='\''&'\''/p

		q
	}'

	logmsg=`git show -s --pretty=raw --encoding="$encoding" "$commit"`
	set_author_env=`echo "$logmsg" |
	LANG=C LC_ALL=C sed -ne "$pick_author_script"`
	eval "$set_author_env"
	export GIT_AUTHOR_NAME
	export GIT_AUTHOR_EMAIL
	export GIT_AUTHOR_DATE

	echo "$logmsg" |
	sed -e '1,/^$/d' -e 's/^    //'
	case "$replay" in
	'')
		echo "(cherry picked from commit $commit)"
		test "$rev" = "$commit" ||
		echo "(original 'git cherry-pick' arguments: $@)"
		;;
	esac
	base=$prev next=$commit
	;;

esac >.msg

eval GITHEAD_$head=HEAD
eval GITHEAD_$next='`git show -s \
	--pretty=oneline --encoding="$encoding" "$commit" |
	sed -e "s/^[^ ]* //"`'
export GITHEAD_$head GITHEAD_$next

# This three way merge is an interesting one.  We are at
# $head, and would want to apply the change between $commit
# and $prev on top of us (when reverting), or the change between
# $prev and $commit on top of us (when cherry-picking or replaying).

git-merge-recursive $base -- $head $next &&
result=$(git-write-tree 2>/dev/null) || {
	mv -f .msg "$GIT_DIR/MERGE_MSG"
	{
	    echo '
Conflicts:
'
		git ls-files --unmerged |
		sed -e 's/^[^	]*	/	/' |
		uniq
	} >>"$GIT_DIR/MERGE_MSG"
	echo >&2 "Automatic $me failed.  After resolving the conflicts,"
	echo >&2 "mark the corrected paths with 'git-add <paths>'"
	echo >&2 "and commit the result."
	case "$me" in
	cherry-pick)
		echo >&2 "You may choose to use the following when making"
		echo >&2 "the commit:"
		echo >&2 "$set_author_env"
	esac
	exit 1
}
echo >&2 "Finished one $me."

# If we are cherry-pick, and if the merge did not result in
# hand-editing, we will hit this commit and inherit the original
# author date and name.
# If we are revert, or if our cherry-pick results in a hand merge,
# we had better say that the current user is responsible for that.

case "$no_commit" in
'')
	git-commit -n -F .msg $edit
	rm -f .msg
	;;
esac
