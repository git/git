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

# The other head is given.  Make sure it is valid.
other=$(git-rev-parse --verify "$1^0") || usage

# Make sure we have HEAD that is valid.
head=$(git-rev-parse --verify "HEAD^0") || exit

# The tree must be really really clean.
git-update-index --refresh || exit
diff=$(git-diff-index --cached --name-status -r HEAD)
case "$different" in
?*)	echo "$diff"
	exit 1
	;;
esac

# If the branch to rebase is given, first switch to it.
case "$#" in
2)
	head=$(git-rev-parse --verify "$2^") || usage
	git-checkout "$2" || usage
esac

# If the HEAD is a proper descendant of $other, we do not even need
# to rebase.  Make sure we do not do needless rebase.  In such a
# case, merge-base should be the same as "$other".
mb=$(git-merge-base "$other" "$head")
if test "$mb" = "$other"
then
	echo >&2 "Current branch `git-symbolic-ref HEAD` is up to date."
	exit 0
fi

# Rewind the head to "$other"
git-reset --hard "$other"
git-format-patch -k --stdout --full-index "$other" ORIG_HEAD |
git am --binary -3 -k
