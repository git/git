#!/bin/sh
#
# Copyright (c) 2006, Shawn O. Pearce
#
# Cleanup unreachable files and optimize the repository.

USAGE='git-gc [--prune]'
SUBDIRECTORY_OK=Yes
. git-sh-setup

no_prune=:
while case $# in 0) break ;; esac
do
	case "$1" in
	--prune)
		no_prune=
		;;
	--)
		usage
		;;
	esac
	shift
done

git-pack-refs --prune &&
git-reflog expire --all &&
git-repack -a -d -l &&
$no_prune git-prune &&
git-rerere gc || exit
