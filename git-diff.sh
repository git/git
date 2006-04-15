#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
# Copyright (c) 2005 Junio C Hamano

USAGE='[ --diff-options ] <ent>{0,2} [<path>...]'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

rev=$(git-rev-parse --revs-only --no-flags --sq "$@") || exit
flags=$(git-rev-parse --no-revs --flags --sq "$@")
files=$(git-rev-parse --no-revs --no-flags --sq "$@")

# I often say 'git diff --cached -p' and get scolded by git-diff-files, but
# obviously I mean 'git diff --cached -p HEAD' in that case.
case "$rev" in
'')
	case " $flags " in
	*" '--cached' "*)
		rev='HEAD '
		;;
	esac
esac

# If we have -[123] --ours --theirs --base, don't do --cc by default.
case " $flags " in
*" '-"[123]"' "* | *" '--ours' "* | *" '--base' "* | *" '--theirs' "*)
	cc_or_p=-p ;;
*)
	cc_or_p=--cc ;;
esac

# If we do not have --name-status, --name-only, -r, -c or --stat,
# default to --cc.
case " $flags " in
*" '--name-status' "* | *" '--name-only' "* | *" '-r' "* | *" '-c' "* | \
*" '--stat' "*)
	;;
*)
	flags="$flags'$cc_or_p' " ;;
esac

# If we do not have -B, -C, -r, nor -p, default to -M.
case " $flags " in
*" '-"[BCMrp]* | *" '--find-copies-harder' "*)
	;; # something like -M50.
*)
	flags="$flags'-M' " ;;
esac

case "$rev" in
?*' '?*' '?*)
	usage
	;;
?*' '^?*)
	begin=$(expr "$rev" : '.*^.\([0-9a-f]*\).*') &&
	end=$(expr "$rev" : '.\([0-9a-f]*\). .*') || exit
	cmd="git-diff-tree $flags $begin $end -- $files"
	;;
?*' '?*)
	cmd="git-diff-tree $flags $rev -- $files"
	;;
?*' ')
	cmd="git-diff-index $flags $rev -- $files"
	;;
'')
	cmd="git-diff-files $flags -- $files"
	;;
*)
	usage
	;;
esac

eval "$cmd"
