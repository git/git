#!/bin/sh
# This requires a branch named in $head
# (usually 'man' or 'html', provided by the git.git repository)
set -e
head="$1"
mandir="$2"
SUBDIRECTORY_OK=t
USAGE='<refname> <target directory>'
. "$(git --exec-path)"/git-sh-setup
cd_to_toplevel

test -z "$mandir" && usage
if ! git rev-parse --verify "$head^0" >/dev/null; then
	echo >&2 "head: $head does not exist in the current repository"
	usage
fi

GIT_INDEX_FILE=`pwd`/.quick-doc.index
export GIT_INDEX_FILE
rm -f "$GIT_INDEX_FILE"
trap 'rm -f "$GIT_INDEX_FILE"' 0

git read-tree $head
git checkout-index -a -f --prefix="$mandir"/

if test -n "$GZ"; then
	git ls-tree -r --name-only $head |
	xargs printf "$mandir/%s\n" |
	xargs gzip -f
fi
rm -f "$GIT_INDEX_FILE"
