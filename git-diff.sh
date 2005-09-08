#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
# Copyright (c) 2005 Junio C Hamano

rev=$(git-rev-parse --revs-only --no-flags --sq "$@") || exit
flags=$(git-rev-parse --no-revs --flags --sq "$@")
files=$(git-rev-parse --no-revs --no-flags --sq "$@")

: ${flags:="'-M' '-p'"}

case "$rev" in
?*' '?*' '?*)
	die "I don't understand"
	;;
?*' '^?*)
	begin=$(expr "$rev" : '.*^.\([0-9a-f]*\).*') &&
	end=$(expr "$rev" : '.\([0-9a-f]*\). .*') || exit
	cmd="git-diff-tree $flags $begin $end $files"
	;;
?*' '?*)
	cmd="git-diff-tree $flags $rev $files"
	;;
?*' ')
	cmd="git-diff-index $flags $rev $files"
	;;
'')
	cmd="git-diff-files $flags $files"
	;;
*)
	die "I don't understand $*"
	;;
esac

eval "$cmd"
