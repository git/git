#!/bin/sh
#
# Copyright (c) Linus Torvalds, 2005
#

USAGE='<option>... <pattern> <path>...'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

pattern=
flags=()
git_flags=()
while : ; do
	case "$1" in
	--cached|--deleted|--others|--killed|\
	--ignored|--exclude=*|\
	--exclude-from=*|\--exclude-per-directory=*)
		git_flags=("${git_flags[@]}" "$1")
		;;
	-e)
		pattern="$2"
		shift
		;;
	-A|-B|-C|-D|-d|-f|-m)
		flags=("${flags[@]}" "$1" "$2")
		shift
		;;
	--)
		# The rest are git-ls-files paths (or flags)
		shift
		break
		;;
	-*)
		flags=("${flags[@]}" "$1")
		;;
	*)
		if [ -z "$pattern" ]; then
			pattern="$1"
			shift
		fi
		break
		;;
	esac
	shift
done
[ "$pattern" ] || {
	usage
}
git-ls-files -z "${git_flags[@]}" "$@" |
	xargs -0 grep "${flags[@]}" -e "$pattern"
