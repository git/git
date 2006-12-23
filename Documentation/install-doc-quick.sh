#!/bin/sh
# This requires a branch named in $head
# (usually 'man' or 'html', provided by the git.git repository)
set -e
head="$1"
mandir="$2"
SUBDIRECTORY_OK=t
USAGE='<refname> <target directory>'
. git-sh-setup
export GIT_DIR

test -z "$mandir" && usage
if ! git-rev-parse --verify "$head^0" >/dev/null; then
	echo >&2 "head: $head does not exist in the current repository"
	usage
fi

GIT_INDEX_FILE=`pwd`/.quick-doc.index
export GIT_INDEX_FILE
rm -f "$GIT_INDEX_FILE"
git-read-tree $head
git-checkout-index -a -f --prefix="$mandir"/

if test -n "$GZ"; then
	cd "$mandir"
	for i in `git-ls-tree -r --name-only $head`
	do
		gzip < $i > $i.gz && rm $i
	done
fi
rm -f "$GIT_INDEX_FILE"
