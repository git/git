#!/bin/sh
# Copyright 2005, Ryan Anderson <ryan@michonline.com>
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Linus Torvalds.

USAGE='<start> <url> [<end>]'
LONG_USAGE='Summarizes the changes between two commits to the standard output,
and includes the given URL in the generated summary.'
SUBDIRECTORY_OK='Yes'
OPTIONS_KEEPDASHDASH=
OPTIONS_SPEC='git request-pull [options] start url [end]
--
p    show patch text as well
'

. git-sh-setup

GIT_PAGER=
export GIT_PAGER

patch=
while	case "$#" in 0) break ;; esac
do
	case "$1" in
	-p)
		patch=-p ;;
	--)
		shift; break ;;
	-*)
		usage ;;
	*)
		break ;;
	esac
	shift
done

base=$1 url=$2 head=${3-HEAD} status=0 branch_name=

headref=$(git symbolic-ref -q "$head")
if git show-ref -q --verify "$headref"
then
	branch_name=${headref#refs/heads/}
	if test "z$branch_name" = "z$headref" ||
		! git config "branch.$branch_name.description" >/dev/null
	then
		branch_name=
	fi
fi

test -n "$base" && test -n "$url" || usage
baserev=$(git rev-parse --verify "$base"^0) &&
headrev=$(git rev-parse --verify "$head"^0) || exit

merge_base=$(git merge-base $baserev $headrev) ||
die "fatal: No commits in common between $base and $head"

find_matching_branch="/^$headrev	"'refs\/heads\//{
	s/^.*	refs\/heads\///
	p
	q
}'
branch=$(git ls-remote "$url" | sed -n -e "$find_matching_branch")
url=$(git ls-remote --get-url "$url")

git show -s --format='The following changes since commit %H:

  %s (%ci)

are available in the git repository at:
' $baserev &&
echo "  $url${branch+ $branch}" &&
git show -s --format='
for you to fetch changes up to %H:

  %s (%ci)

----------------------------------------------------------------' $headrev &&

if test -n "$branch_name"
then
	echo "(from the branch description for $branch local branch)"
	echo
	git config "branch.$branch_name.description"
	echo "----------------------------------------------------------------"
fi &&
git shortlog ^$baserev $headrev &&
git diff -M --stat --summary $patch $merge_base..$headrev || status=1

if test -z "$branch"
then
	echo "warn: No branch of $url is at:" >&2
	git show -s --format='warn:   %h: %s' $headrev >&2
	echo "warn: Are you sure you pushed '$head' there?" >&2
	status=1
fi
exit $status
