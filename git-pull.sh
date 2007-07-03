#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#
# Fetch one or more remote refs and merge it/them into the current HEAD.

USAGE='[-n | --no-summary] [--no-commit] [-s strategy]... [<fetch-options>] <repo> <head>...'
LONG_USAGE='Fetch one or more remote refs and merge it/them into the current HEAD.'
SUBDIRECTORY_OK=Yes
. git-sh-setup
set_reflog_action "pull $*"
require_work_tree
cd_to_toplevel

test -z "$(git ls-files -u)" ||
	die "You are in the middle of a conflicted merge."

strategy_args= no_summary= no_commit= squash=
while case "$#,$1" in 0) break ;; *,-*) ;; *) break ;; esac
do
	case "$1" in
	-n|--n|--no|--no-|--no-s|--no-su|--no-sum|--no-summ|\
		--no-summa|--no-summar|--no-summary)
		no_summary=-n ;;
	--summary)
		no_summary=$1
		;;
	--no-c|--no-co|--no-com|--no-comm|--no-commi|--no-commit)
		no_commit=--no-commit ;;
	--sq|--squ|--squa|--squas|--squash)
		squash=--squash ;;
	-s=*|--s=*|--st=*|--str=*|--stra=*|--strat=*|--strate=*|\
		--strateg=*|--strategy=*|\
	-s|--s|--st|--str|--stra|--strat|--strate|--strateg|--strategy)
		case "$#,$1" in
		*,*=*)
			strategy=`expr "z$1" : 'z-[^=]*=\(.*\)'` ;;
		1,*)
			usage ;;
		*)
			strategy="$2"
			shift ;;
		esac
		strategy_args="${strategy_args}-s $strategy "
		;;
	-h|--h|--he|--hel|--help)
		usage
		;;
	-*)
		# Pass thru anything that is meant for fetch.
		break
		;;
	esac
	shift
done

orig_head=$(git rev-parse --verify HEAD 2>/dev/null)
git-fetch --update-head-ok "$@" || exit 1

curr_head=$(git rev-parse --verify HEAD 2>/dev/null)
if test "$curr_head" != "$orig_head"
then
	# The fetch involved updating the current branch.

	# The working tree and the index file is still based on the
	# $orig_head commit, but we are merging into $curr_head.
	# First update the working tree to match $curr_head.

	echo >&2 "Warning: fetch updated the current branch head."
	echo >&2 "Warning: fast forwarding your working tree from"
	echo >&2 "Warning: commit $orig_head."
	git update-index --refresh 2>/dev/null
	git read-tree -u -m "$orig_head" "$curr_head" ||
		die 'Cannot fast-forward your working tree.
After making sure that you saved anything precious from
$ git diff '$orig_head'
output, run
$ git reset --hard
to recover.'

fi

merge_head=$(sed -e '/	not-for-merge	/d' \
	-e 's/	.*//' "$GIT_DIR"/FETCH_HEAD | \
	tr '\012' ' ')

case "$merge_head" in
'')
	curr_branch=$(git symbolic-ref -q HEAD)
	case $? in
	  0) ;;
	  1) echo >&2 "You are not currently on a branch; you must explicitly"
	     echo >&2 "specify which branch you wish to merge:"
	     echo >&2 "  git pull <remote> <branch>"
	     exit 1;;
	  *) exit $?;;
	esac
	curr_branch=${curr_branch#refs/heads/}

	echo >&2 "Warning: No merge candidate found because value of config option
         \"branch.${curr_branch}.merge\" does not match any remote branch fetched."
	echo >&2 "No changes."
	exit 0
	;;
?*' '?*)
	if test -z "$orig_head"
	then
		echo >&2 "Cannot merge multiple branches into empty head"
		exit 1
	fi
	;;
esac

if test -z "$orig_head"
then
	git update-ref -m "initial pull" HEAD $merge_head "" &&
	git read-tree --reset -u HEAD || exit 1
	exit
fi

merge_name=$(git fmt-merge-msg <"$GIT_DIR/FETCH_HEAD") || exit
exec git-merge $no_summary $no_commit $squash $strategy_args \
	"$merge_name" HEAD $merge_head
