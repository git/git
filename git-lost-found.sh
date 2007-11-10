#!/bin/sh

USAGE=''
SUBDIRECTORY_OK='Yes'
. git-sh-setup

echo "WARNING: '$0' is deprecated in favor of 'git fsck --lost-found'" >&2

if [ "$#" != "0" ]
then
    usage
fi

laf="$GIT_DIR/lost-found"
rm -fr "$laf" && mkdir -p "$laf/commit" "$laf/other" || exit

git fsck --full --no-reflogs |
while read dangling type sha1
do
	case "$dangling" in
	dangling)
		if git rev-parse --verify "$sha1^0" >/dev/null 2>/dev/null
		then
			dir="$laf/commit"
			git show-branch "$sha1"
		else
			dir="$laf/other"
		fi
		echo "$sha1" >"$dir/$sha1"
		;;
	esac
done
