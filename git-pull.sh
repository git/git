#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#
# Fetch one or more remote refs and merge it/them into the current HEAD.

USAGE='[-n | --no-summary] [--no-commit] [-s strategy]... [<fetch-options>] <repo> <head>...'
LONG_USAGE='Fetch one or more remote refs and merge it/them into the current HEAD.'
. git-sh-setup

strategy_args= no_summary= no_commit=
while case "$#,$1" in 0) break ;; *,-*) ;; *) break ;; esac
do
	case "$1" in
	-n|--n|--no|--no-|--no-s|--no-su|--no-sum|--no-summ|\
		--no-summa|--no-summar|--no-summary)
		no_summary=-n ;;
	--no-c|--no-co|--no-com|--no-comm|--no-commi|--no-commit)
		no_commit=--no-commit ;;
	-s=*|--s=*|--st=*|--str=*|--stra=*|--strat=*|--strate=*|\
		--strateg=*|--strategy=*|\
	-s|--s|--st|--str|--stra|--strat|--strate|--strateg|--strategy)
		case "$#,$1" in
		*,*=*)
			strategy=`expr "$1" : '-[^=]*=\(.*\)'` ;;
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

orig_head=$(git-rev-parse --verify HEAD) || die "Pulling into a black hole?"
git-fetch --update-head-ok "$@" || exit 1

curr_head=$(git-rev-parse --verify HEAD)
if test "$curr_head" != "$orig_head"
then
	# The fetch involved updating the current branch.

	# The working tree and the index file is still based on the
	# $orig_head commit, but we are merging into $curr_head.
	# First update the working tree to match $curr_head.

	echo >&2 "Warning: fetch updated the current branch head."
	echo >&2 "Warning: fast forwarding your working tree from"
	echo >&2 "Warning: $orig_head commit."
	git-update-index --refresh 2>/dev/null
	git-read-tree -u -m "$orig_head" "$curr_head" ||
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
	echo >&2 "No changes."
	exit 0
	;;
?*' '?*)
	var=`git-repo-config --get pull.octopus`
	if test -n "$var"
	then
		strategy_default_args="-s $var"
	fi
	;;
*)
	var=`git-repo-config --get pull.twohead`
	if test -n "$var"
        then
		strategy_default_args="-s $var"
	fi
	;;
esac

case "$strategy_args" in
'')
	strategy_args=$strategy_default_args
	;;
esac

merge_name=$(git-fmt-merge-msg <"$GIT_DIR/FETCH_HEAD") || exit
git-merge $no_summary $no_commit $strategy_args "$merge_name" HEAD $merge_head
