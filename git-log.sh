#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#

# This one uses only subdirectory-aware commands, so no need to
# include sh-setup-script.

revs=$(git-rev-parse --revs-only --no-flags --default HEAD "$@") || exit
[ "$revs" ] || {
	echo >&2 "No HEAD ref"
	exit 1
}
git-rev-list --pretty $(git-rev-parse --default HEAD "$@") |
LESS=-S ${PAGER:-less}
