#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

USAGE='[--onto <newbase>] <upstream> [<branch>]'
LONG_USAGE='git-rebase replaces <branch> with a new branch of the
same name.  When the --onto option is provided the new branch starts
out with a HEAD equal to <newbase>, otherwise it is equal to <upstream>
It then attempts to create a new commit for each commit from the original
<branch> that does not exist in the <upstream> branch.

It is possible that a merge failure will prevent this process from being
completely automatic.  You will have to resolve any such merge failure
and run git rebase --continue.  Another option is to bypass the commit
that caused the merge failure with git rebase --skip.  To restore the
original <branch> and remove the .dotest working files, use the command
git rebase --abort instead.

Note that if <branch> is not specified on the command line, the
currently checked out branch is used.  You must be in the top
directory of your project to start (or continue) a rebase.

Example:       git-rebase master~1 topic

        A---B---C topic                   A'\''--B'\''--C'\'' topic
       /                   -->           /
  D---E---F---G master          D---E---F---G master
'
. git-sh-setup

RESOLVEMSG="
When you have resolved this problem run \"git rebase --continue\".
If you would prefer to skip this patch, instead run \"git rebase --skip\".
To restore the original branch and stop rebasing run \"git rebase --abort\".
"
unset newbase
while case "$#" in 0) break ;; esac
do
	case "$1" in
	--continue)
		diff=$(git-diff-files)
		case "$diff" in
		?*)	echo "You must edit all merge conflicts and then"
			echo "mark them as resolved using git update-index"
			exit 1
			;;
		esac
		git am --resolved --3way --resolvemsg="$RESOLVEMSG"
		exit
		;;
	--skip)
		git am -3 --skip --resolvemsg="$RESOLVEMSG"
		exit
		;;
	--abort)
		[ -d .dotest ] || die "No rebase in progress?"
		git reset --hard ORIG_HEAD
		rm -r .dotest
		exit
		;;
	--onto)
		test 2 -le "$#" || usage
		newbase="$2"
		shift
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

# Make sure we do not have .dotest
if mkdir .dotest
then
	rmdir .dotest
else
	echo >&2 '
It seems that I cannot create a .dotest directory, and I wonder if you
are in the middle of patch application or another rebase.  If that is not
the case, please rm -fr .dotest and run me again.  I am stopping in case
you still have something valuable there.'
	exit 1
fi

# The tree must be really really clean.
git-update-index --refresh || exit
diff=$(git-diff-index --cached --name-status -r HEAD)
case "$diff" in
?*)	echo "$diff"
	exit 1
	;;
esac

# The upstream head must be given.  Make sure it is valid.
upstream_name="$1"
upstream=`git rev-parse --verify "${upstream_name}^0"` ||
    die "invalid upstream $upstream_name"

# If a hook exists, give it a chance to interrupt
if test -x "$GIT_DIR/hooks/pre-rebase"
then
	"$GIT_DIR/hooks/pre-rebase" ${1+"$@"} || {
		echo >&2 "The pre-rebase hook refused to rebase."
		exit 1
	}
fi

# If the branch to rebase is given, first switch to it.
case "$#" in
2)
	branch_name="$2"
	git-checkout "$2" || usage
	;;
*)
	branch_name=`git symbolic-ref HEAD` || die "No current branch"
	branch_name=`expr "z$branch_name" : 'zrefs/heads/\(.*\)'`
	;;
esac
branch=$(git-rev-parse --verify "${branch_name}^0") || exit

# Make sure the branch to rebase onto is valid.
onto_name=${newbase-"$upstream_name"}
onto=$(git-rev-parse --verify "${onto_name}^0") || exit

# Now we are rebasing commits $upstream..$branch on top of $onto

# Check if we are already based on $onto, but this should be
# done only when upstream and onto are the same.
if test "$upstream" = "$onto"
then
	mb=$(git-merge-base "$onto" "$branch")
	if test "$mb" = "$onto"
	then
		echo >&2 "Current branch $branch_name is up to date."
		exit 0
	fi
fi

# Rewind the head to "$onto"; this saves our current head in ORIG_HEAD.
git-reset --hard "$onto"

# If the $onto is a proper descendant of the tip of the branch, then
# we just fast forwarded.
if test "$mb" = "$onto"
then
	echo >&2 "Fast-forwarded $branch to $newbase."
	exit 0
fi

git-format-patch -k --stdout --full-index "$upstream"..ORIG_HEAD |
git am --binary -3 -k --resolvemsg="$RESOLVEMSG"

