#!/bin/sh
#
# Copyright (c) Linus Torvalds, 2005
#

USAGE='[<option>...] [-e] <pattern> [<path>...]'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

got_pattern () {
	if [ -z "$no_more_patterns" ]
	then
		pattern="$1" no_more_patterns=yes
	else
		die "git-grep: do not specify more than one pattern"
	fi
}

no_more_patterns=
pattern=
flags=()
git_flags=()
while : ; do
	case "$1" in
	-o|--cached|--deleted|--others|--killed|\
	--ignored|--modified|--exclude=*|\
	--exclude-from=*|\--exclude-per-directory=*)
		git_flags=("${git_flags[@]}" "$1")
		;;
	-e)
		got_pattern "$2"
		shift
		;;
	-A|-B|-C|-D|-d|-f|-m)
		flags=("${flags[@]}" "$1" "$2")
		shift
		;;
	--)
		# The rest are git-ls-files paths
		shift
		break
		;;
	-*)
		flags=("${flags[@]}" "$1")
		;;
	*)
		if [ -z "$no_more_patterns" ]
		then
			got_pattern "$1"
			shift
		fi
		[ "$1" = -- ] && shift
		break
		;;
	esac
	shift
done
[ "$pattern" ] || {
	usage
}
git-ls-files -z "${git_flags[@]}" -- "$@" |
	xargs -0 grep "${flags[@]}" -e "$pattern" --
