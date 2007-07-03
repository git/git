#!/bin/sh
#
# Copyright (c) 2006, Shawn O. Pearce
#
# Cleanup unreachable files and optimize the repository.

USAGE='[--prune]'
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

case "$(git config --get gc.packrefs)" in
notbare|"")
	test $(is_bare_repository) = true || pack_refs=true;;
*)
	pack_refs=$(git config --bool --get gc.packrefs)
esac

test "true" != "$pack_refs" ||
git pack-refs --prune &&
git reflog expire --all &&
git-repack -a -d -l &&
$no_prune git prune &&
git rerere gc || exit
