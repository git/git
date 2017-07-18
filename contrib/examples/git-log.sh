#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#

USAGE='[--max-count=<n>] [<since>..<limit>] [--pretty=<format>] [git-rev-list options]'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

revs=$(git-rev-parse --revs-only --no-flags --default HEAD "$@") || exit
[ "$revs" ] || {
	die "No HEAD ref"
}
git-rev-list --pretty $(git-rev-parse --default HEAD "$@") |
LESS=-S ${PAGER:-less}
