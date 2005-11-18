#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

. git-sh-setup || die "Not a git archive."

# The other head is given
other=$(git-rev-parse --verify "$1^0") || exit

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
	git-checkout "$2" || exit
esac

# Rewind the head to "$other"
git-reset --hard "$other"
git-format-patch -k --stdout --full-index "$other" ORIG_HEAD |
git am --binary -3 -k
