#!/bin/sh -e
# Copyright 2005, Ryan Anderson <ryan@michonline.com>
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Linus Torvalds.

USAGE='<commit> <url> [<head>]'
LONG_USAGE='Summarizes the changes since <commit> to the standard output,
and includes <url> in the message generated.'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

revision=$1
url=$2
head=${3-HEAD}

[ "$revision" ] || usage
[ "$url" ] || usage

baserev=`git-rev-parse --verify "$revision"^0` &&
headrev=`git-rev-parse --verify "$head"^0` || exit

echo "The following changes since commit $baserev:"
git log --max-count=1 --pretty=short "$baserev" |
git-shortlog | sed -e 's/^\(.\)/  \1/'

echo "are found in the git repository at:" 
echo
echo "  $url"
echo

git log  $baserev..$headrev | git-shortlog ;
git diff -M --stat --summary $baserev..$headrev
