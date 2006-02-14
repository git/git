#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

USAGE='[--onto <newbase>] <upstream> [<branch>]'
LONG_USAGE='If <branch> is specified, switch to that branch first.  Then,
extract commits in the current branch that are not in <upstream>,
and reconstruct the current on top of <upstream>, discarding the original
development history.  If --onto <newbase> is specified, the history is
reconstructed on top of <newbase>, instead of <upstream>.  For example,
while on "topic" branch:

          A---B---C topic
         /
    D---E---F---G master

	$ '"$0"' --onto master~1 master topic

would rewrite the history to look like this:


	      A'\''--B'\''--C'\'' topic
	     /
    D---E---F---G master
'

. git-sh-setup

unset newbase
while case "$#" in 0) break ;; esac
do
	case "$1" in
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
    die "invalid upsteram $upstream_name"

# If the branch to rebase is given, first switch to it.
case "$#" in
2)
	branch_name="$2"
	git-checkout "$2" || usage
	;;
*)
	branch_name=`git symbolic-ref HEAD` || die "No current branch"
	branch_name=`expr "$branch_name" : 'refs/heads/\(.*\)'`
	;;
esac
branch=$(git-rev-parse --verify "${branch_name}^0") || exit

# Make sure the branch to rebase onto is valid.
onto_name=${newbase-"$upstream_name"}
onto=$(git-rev-parse --verify "${onto_name}^0") || exit

# Now we are rebasing commits $upstream..$branch on top of $onto

# Check if we are already based on $onto, but this should be
# done only when upstream and onto are the same.
if test "$upstream" = "onto"
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

git-format-patch -k --stdout --full-index "$upstream" ORIG_HEAD |
git am --binary -3 -k
