#!/bin/sh
# This requires but-manpages and/or but-htmldocs repositories

repository=${1?repository}
destdir=${2?destination}
BUT_MAN_REF=${3?master}

BUT_DIR=
for d in "$repository/.but" "$repository"
do
	if BUT_DIR="$d" but rev-parse "$BUT_MAN_REF" >/dev/null 2>&1
	then
		BUT_DIR="$d"
		export BUT_DIR
		break
	fi
done

if test -z "$BUT_DIR"
then
	echo >&2 "Neither $repository nor $repository/.but is a repository"
	exit 1
fi

BUT_WORK_TREE=$(pwd)
BUT_INDEX_FILE=$(pwd)/.quick-doc.$$
export BUT_INDEX_FILE BUT_WORK_TREE
rm -f "$BUT_INDEX_FILE"
trap 'rm -f "$BUT_INDEX_FILE"' 0

but read-tree "$BUT_MAN_REF"
but checkout-index -a -f --prefix="$destdir"/

if test -n "$GZ"
then
	but ls-tree -r --name-only "$BUT_MAN_REF" |
	xargs printf "$destdir/%s\n" |
	xargs gzip -f
fi
rm -f "$BUT_INDEX_FILE"
