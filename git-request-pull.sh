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

base=$1 url=$2 status=0

test -n "$base" && test -n "$url" || usage

baserev=$(git rev-parse --verify --quiet "$base"^0)
if test -z "$baserev"
then
    die "fatal: Not a valid revision: $base"
fi

#
# $3 must be a symbolic ref, a unique ref, or
# a SHA object expression
#
head=$(git symbolic-ref -q "${3-HEAD}")
head=${head:-$(git show-ref "${3-HEAD}" | cut -d' ' -f2)}
head=${head:-$(git rev-parse --quiet --verify "$3")}

# None of the above? Bad.
test -z "$head" && die "fatal: Not a valid revision: $3"

# This also verifies that the resulting head is unique:
# "git show-ref" could have shown multiple matching refs..
headrev=$(git rev-parse --verify --quiet "$head"^0)
test -z "$headrev" && die "fatal: Ambiguous revision: $3"

# Was it a branch with a description?
branch_name=${head#refs/heads/}
if test "z$branch_name" = "z$headref" ||
	! git config "branch.$branch_name.description" >/dev/null
then
	branch_name=
fi

prettyhead=${head#refs/}
prettyhead=${prettyhead#heads/}

merge_base=$(git merge-base $baserev $headrev) ||
die "fatal: No commits in common between $base and $head"

# $head is the refname from the command line.
# If a ref with the same name as $head exists at the remote
# and their values match, use that.
#
# Otherwise find a random ref that matches $headrev.
find_matching_ref='
	my ($exact,$found);
	while (<STDIN>) {
		my ($sha1, $ref, $deref) = /^(\S+)\s+([^^]+)(\S*)$/;
		next unless ($sha1 eq $ARGV[1]);
		if ($ref eq $ARGV[0]) {
			$exact = $ref;
		}
		if ($sha1 eq $ARGV[0]) {
			$found = $sha1;
		}
	}
	if ($exact) {
		print "$exact\n";
	} elsif ($found) {
		print "$found\n";
	}
'

ref=$(git ls-remote "$url" | @@PERL@@ -e "$find_matching_ref" "$head" "$headrev")

if test -z "$ref"
then
	echo "warn: No match for $prettyhead found at $url" >&2
	echo "warn: Are you sure you pushed '$prettyhead' there?" >&2
	status=1
fi

url=$(git ls-remote --get-url "$url")

git show -s --format='The following changes since commit %H:

  %s (%ci)

are available in the git repository at:
' $merge_base &&
echo "  $url $prettyhead" &&
git show -s --format='
for you to fetch changes up to %H:

  %s (%ci)

----------------------------------------------------------------' $headrev &&

if test -n "$branch_name"
then
	echo "(from the branch description for $branch_name local branch)"
	echo
	git config "branch.$branch_name.description"
	echo "----------------------------------------------------------------"
fi &&

git shortlog ^$baserev $headrev &&
git diff -M --stat --summary $patch $merge_base..$headrev || status=1

exit $status
