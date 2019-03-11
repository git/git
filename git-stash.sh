#!/bin/sh
# Copyright (c) 2007, Nanako Shiraishi

dashless=$(basename "$0" | sed -e 's/-/ /')
USAGE="list [<options>]
   or: $dashless show [<stash>]
   or: $dashless drop [-q|--quiet] [<stash>]
   or: $dashless ( pop | apply ) [--index] [-q|--quiet] [<stash>]
   or: $dashless branch <branchname> [<stash>]
   or: $dashless save [--patch] [-k|--[no-]keep-index] [-q|--quiet]
		      [-u|--include-untracked] [-a|--all] [<message>]
   or: $dashless [push [--patch] [-k|--[no-]keep-index] [-q|--quiet]
		       [-u|--include-untracked] [-a|--all] [-m <message>]
		       [-- <pathspec>...]]
   or: $dashless clear"

SUBDIRECTORY_OK=Yes
OPTIONS_SPEC=
START_DIR=$(pwd)
. git-sh-setup
require_work_tree
prefix=$(git rev-parse --show-prefix) || exit 1
cd_to_toplevel

TMP="$GIT_DIR/.git-stash.$$"
TMPindex=${GIT_INDEX_FILE-"$(git rev-parse --git-path index)"}.stash.$$
trap 'rm -f "$TMP-"* "$TMPindex"' 0

ref_stash=refs/stash

if git config --get-colorbool color.interactive; then
       help_color="$(git config --get-color color.interactive.help 'red bold')"
       reset_color="$(git config --get-color '' reset)"
else
       help_color=
       reset_color=
fi

#
# Parses the remaining options looking for flags and
# at most one revision defaulting to ${ref_stash}@{0}
# if none found.
#
# Derives related tree and commit objects from the
# revision, if one is found.
#
# stash records the work tree, and is a merge between the
# base commit (first parent) and the index tree (second parent).
#
#   REV is set to the symbolic version of the specified stash-like commit
#   IS_STASH_LIKE is non-blank if ${REV} looks like a stash
#   IS_STASH_REF is non-blank if the ${REV} looks like a stash ref
#   s is set to the SHA1 of the stash commit
#   w_commit is set to the commit containing the working tree
#   b_commit is set to the base commit
#   i_commit is set to the commit containing the index tree
#   u_commit is set to the commit containing the untracked files tree
#   w_tree is set to the working tree
#   b_tree is set to the base tree
#   i_tree is set to the index tree
#   u_tree is set to the untracked files tree
#
#   GIT_QUIET is set to t if -q is specified
#   INDEX_OPTION is set to --index if --index is specified.
#   FLAGS is set to the remaining flags (if allowed)
#
# dies if:
#   * too many revisions specified
#   * no revision is specified and there is no stash stack
#   * a revision is specified which cannot be resolve to a SHA1
#   * a non-existent stash reference is specified
#   * unknown flags were set and ALLOW_UNKNOWN_FLAGS is not "t"
#

test "$1" = "-p" && set "push" "$@"

PARSE_CACHE='--not-parsed'
# The default command is "push" if nothing but options are given
seen_non_option=
for opt
do
	case "$opt" in
	--) break ;;
	-*) ;;
	*) seen_non_option=t; break ;;
	esac
done

test -n "$seen_non_option" || set "push" "$@"

# Main command set
case "$1" in
list)
	shift
	git stash--helper list "$@"
	;;
show)
	shift
	git stash--helper show "$@"
	;;
save)
	shift
	cd "$START_DIR"
	git stash--helper save "$@"
	;;
push)
	shift
	cd "$START_DIR"
	git stash--helper push "$@"
	;;
apply)
	shift
	cd "$START_DIR"
	git stash--helper apply "$@"
	;;
clear)
	shift
	git stash--helper clear "$@"
	;;
create)
	shift
	git stash--helper create --message "$*"
	;;
store)
	shift
	git stash--helper store "$@"
	;;
drop)
	shift
	git stash--helper drop "$@"
	;;
pop)
	shift
	cd "$START_DIR"
	git stash--helper pop "$@"
	;;
branch)
	shift
	cd "$START_DIR"
	git stash--helper branch "$@"
	;;
*)
	case $# in
	0)
		cd "$START_DIR"
		git stash--helper push &&
		say "$(gettext "(To restore them type \"git stash apply\")")"
		;;
	*)
		usage
	esac
	;;
esac
