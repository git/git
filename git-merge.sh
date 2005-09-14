#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

. git-sh-setup || die "Not a git archive"

LF='
'

usage () {
    die "git-merge [-n] [-s <strategy>]... <merge-message> <head> <remote>+"
}

# all_strategies='resolve recursive stupid octopus'

all_strategies='recursive octopus resolve stupid'
default_strategies='resolve octopus'
use_strategies=

dropheads() {
	rm -f -- "$GIT_DIR/MERGE_HEAD" || exit 1
}

summary() {
	case "$no_summary" in
	'')
		git-diff-tree -p -M $head "$1" |
		git-apply --stat --summary
		;;
	esac
}

while case "$#" in 0) break ;; esac
do
	case "$1" in
	-n|--n|--no|--no-|--no-s|--no-su|--no-sum|--no-summ|\
		--no-summa|--no-summar|--no-summary)
		no_summary=t ;;
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
		case " $all_strategies " in
		*" $strategy "*)
			use_strategies="$use_strategies$strategy " ;;
		*)
			die "available strategies are: $all_strategies" ;;
		esac
		;;
	-*)	usage ;;
	*)	break ;;
	esac
	shift
done

case "$use_strategies" in
'')
	use_strategies=$default_strategies
	;;
esac
test "$#" -le 2 && usage ;# we need at least two heads.

merge_msg="$1"
shift
head=$(git-rev-parse --verify "$1"^0) || usage
shift

# All the rest are remote heads
for remote
do
	git-rev-parse --verify "$remote"^0 >/dev/null ||
	    die "$remote - not something we can merge"
done

common=$(git-show-branch --merge-base $head "$@")
echo "$head" >"$GIT_DIR/ORIG_HEAD"

case "$#,$common" in
*,'')
	die "Unable to find common commit between $head and $*"
	;;
1,"$1")
	# If head can reach all the merge then we are up to date.
	# but first the most common case of merging one remote
	echo "Already up-to-date. Yeeah!"
	dropheads
	exit 0
	;;
1,"$head")
	# Again the most common case of merging one remote.
	echo "Updating from $head to $1."
	git-update-index --refresh 2>/dev/null
	git-read-tree -u -m $head "$1" || exit 1
	git-rev-parse --verify "$1^0" > "$GIT_DIR/HEAD"
	summary "$1"
	dropheads
	exit 0
	;;
1,*)
	# We are not doing octopus and not fast forward.  Need a
	# real merge.
	;;
*)
	# An octopus.  If we can reach all the remote we are up to date.
	up_to_date=t
	for remote
	do
		common_one=$(git-merge-base $head $remote)
		if test "$common_one" != "$remote"
		then
			up_to_date=f
			break
		fi
	done
	if test "$up_to_date" = t
	then
		echo "Already up-to-date. Yeeah!"
		dropheads
		exit 0
	fi
	;;
esac

# At this point we need a real merge.  Require that the tree matches
# exactly our head.

git-update-index --refresh &&
test '' = "`git-diff-index --cached --name-only $head`" || {
	die "Need real merge but the working tree has local changes."
}

result_tree= best_cnt=-1 best_strategy= wt_strategy=
for strategy in $use_strategies
do
    test "$wt_strategy" = '' || {
	echo "Rewinding the tree to pristine..."
	git reset --hard $head
    }
    echo "Trying merge strategy $strategy..."
    wt_strategy=$strategy
    git-merge-$strategy $common -- $head "$@" || {

	# The backend exits with 1 when conflicts are left to be resolved,
	# with 2 when it does not handle the given merge at all.

	exit=$?
	if test "$exit" -eq 1
	then
	    cnt=`{
		git-diff-files --name-only
		git-ls-files --unmerged
	    } | wc -l`
	    if test $best_cnt -le 0 -o $cnt -le $best_cnt
	    then
		best_strategy=$strategy
		best_cnt=$cnt
	    fi
	fi
	continue
    }

    # Automerge succeeded.
    result_tree=$(git-write-tree) && break
done

# If we have a resulting tree, that means the strategy module
# auto resolved the merge cleanly.
if test '' != "$result_tree"
then
    parents="-p $head"
    for remote
    do
        parents="$parents -p $remote"
    done
    result_commit=$(echo "$merge_msg" | git-commit-tree $result_tree $parents)
    echo "Committed merge $result_commit, made by $wt_strategy."
    echo $result_commit >"$GIT_DIR/HEAD"
    summary $result_commit
    dropheads
    exit 0
fi

# Pick the result from the best strategy and have the user fix it up.
case "$best_strategy" in
'')
	git reset --hard $head
	die "No merge strategy handled the merge."
	;;
"$wt_strategy")
	# We already have its result in the working tree.
	;;
*)
	echo "Rewinding the tree to pristine..."
	git reset --hard $head
	echo "Using the $best_strategy to prepare resolving by hand."
	git-merge-$best_strategy $common -- $head "$@"
	;;
esac
for remote
do
	echo $remote
done >"$GIT_DIR/MERGE_HEAD"
die "Automatic merge failed; fix up by hand"
