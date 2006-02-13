#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

USAGE='<upstream> [<head>]'
. git-sh-setup

case $# in 1|2) ;; *) usage ;; esac

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

# The other head is given.  Make sure it is valid.
other=$(git-rev-parse --verify "$1^0") || usage

# Make sure the branch to rebase is valid.
head=$(git-rev-parse --verify "${2-HEAD}^0") || exit

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
	git-checkout "$2" || usage
esac

mb=$(git-merge-base "$other" "$head")

# Check if we are already based on $other.
if test "$mb" = "$other"
then
	echo >&2 "Current branch `git-symbolic-ref HEAD` is up to date."
	exit 0
fi

# Rewind the head to "$other"
git-reset --hard "$other"

# If the $other is a proper descendant of the tip of the branch, then
# we just fast forwarded.
if test "$mb" = "$head"
then
	echo >&2 "Fast-forwarded $head to $other."
	exit 0
fi

git-format-patch -k --stdout --full-index "$other" ORIG_HEAD |
git am --binary -3 -k
