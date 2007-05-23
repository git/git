#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

USAGE='[-n] [--summary] [--no-commit] [--squash] [-s <strategy>] [-m=<merge-message>] <commit>+'

SUBDIRECTORY_OK=Yes
. git-sh-setup
require_work_tree
cd_to_toplevel

test -z "$(git ls-files -u)" ||
	die "You are in the middle of a conflicted merge."

LF='
'

all_strategies='recur recursive octopus resolve stupid ours subtree'
default_twohead_strategies='recursive'
default_octopus_strategies='octopus'
no_trivial_merge_strategies='ours subtree'
use_strategies=

index_merge=t

dropsave() {
	rm -f -- "$GIT_DIR/MERGE_HEAD" "$GIT_DIR/MERGE_MSG" \
		 "$GIT_DIR/MERGE_SAVE" || exit 1
}

savestate() {
	# Stash away any local modifications.
	git-diff-index -z --name-only $head |
	cpio -0 -o >"$GIT_DIR/MERGE_SAVE"
}

restorestate() {
        if test -f "$GIT_DIR/MERGE_SAVE"
	then
		git reset --hard $head >/dev/null
		cpio -iuv <"$GIT_DIR/MERGE_SAVE"
		git-update-index --refresh >/dev/null
	fi
}

finish_up_to_date () {
	case "$squash" in
	t)
		echo "$1 (nothing to squash)" ;;
	'')
		echo "$1" ;;
	esac
	dropsave
}

squash_message () {
	echo Squashed commit of the following:
	echo
	git-log --no-merges ^"$head" $remote
}

finish () {
	if test '' = "$2"
	then
		rlogm="$GIT_REFLOG_ACTION"
	else
		echo "$2"
		rlogm="$GIT_REFLOG_ACTION: $2"
	fi
	case "$squash" in
	t)
		echo "Squash commit -- not updating HEAD"
		squash_message >"$GIT_DIR/SQUASH_MSG"
		;;
	'')
		case "$merge_msg" in
		'')
			echo "No merge message -- not updating HEAD"
			;;
		*)
			git-update-ref -m "$rlogm" HEAD "$1" "$head" || exit 1
			;;
		esac
		;;
	esac
	case "$1" in
	'')
		;;
	?*)
		if test "$show_diffstat" = t
		then
			# We want color (if set), but no pager
			GIT_PAGER='' git-diff --stat --summary -M "$head" "$1"
		fi
		;;
	esac
}

merge_name () {
	remote="$1"
	rh=$(git-rev-parse --verify "$remote^0" 2>/dev/null) || return
	bh=$(git-show-ref -s --verify "refs/heads/$remote" 2>/dev/null)
	if test "$rh" = "$bh"
	then
		echo "$rh		branch '$remote' of ."
	elif truname=$(expr "$remote" : '\(.*\)~[1-9][0-9]*$') &&
		git-show-ref -q --verify "refs/heads/$truname" 2>/dev/null
	then
		echo "$rh		branch '$truname' (early part) of ."
	elif test "$remote" = "FETCH_HEAD" -a -r "$GIT_DIR/FETCH_HEAD"
	then
		sed -e 's/	not-for-merge	/		/' -e 1q \
			"$GIT_DIR/FETCH_HEAD"
	else
		echo "$rh		commit '$remote'"
	fi
}

case "$#" in 0) usage ;; esac

have_message=
while case "$#" in 0) break ;; esac
do
	case "$1" in
	-n|--n|--no|--no-|--no-s|--no-su|--no-sum|--no-summ|\
		--no-summa|--no-summar|--no-summary)
		show_diffstat=false ;;
	--summary)
		show_diffstat=t ;;
	--sq|--squ|--squa|--squas|--squash)
		squash=t no_commit=t ;;
	--no-c|--no-co|--no-com|--no-comm|--no-commi|--no-commit)
		no_commit=t ;;
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
		case " $all_strategies " in
		*" $strategy "*)
			use_strategies="$use_strategies$strategy " ;;
		*)
			die "available strategies are: $all_strategies" ;;
		esac
		;;
	-m=*|--m=*|--me=*|--mes=*|--mess=*|--messa=*|--messag=*|--message=*)
		merge_msg=`expr "z$1" : 'z-[^=]*=\(.*\)'`
		have_message=t
		;;
	-m|--m|--me|--mes|--mess|--messa|--messag|--message)
		shift
		case "$#" in
		1)	usage ;;
		esac
		merge_msg="$1"
		have_message=t
		;;
	-*)	usage ;;
	*)	break ;;
	esac
	shift
done

if test -z "$show_diffstat"; then
    test "$(git-config --bool merge.diffstat)" = false && show_diffstat=false
    test -z "$show_diffstat" && show_diffstat=t
fi

# This could be traditional "merge <msg> HEAD <commit>..."  and the
# way we can tell it is to see if the second token is HEAD, but some
# people might have misused the interface and used a committish that
# is the same as HEAD there instead.  Traditional format never would
# have "-m" so it is an additional safety measure to check for it.

if test -z "$have_message" &&
	second_token=$(git-rev-parse --verify "$2^0" 2>/dev/null) &&
	head_commit=$(git-rev-parse --verify "HEAD" 2>/dev/null) &&
	test "$second_token" = "$head_commit"
then
	merge_msg="$1"
	shift
	head_arg="$1"
	shift
elif ! git-rev-parse --verify HEAD >/dev/null 2>&1
then
	# If the merged head is a valid one there is no reason to
	# forbid "git merge" into a branch yet to be born.  We do
	# the same for "git pull".
	if test 1 -ne $#
	then
		echo >&2 "Can merge only exactly one commit into empty head"
		exit 1
	fi

	rh=$(git rev-parse --verify "$1^0") ||
		die "$1 - not something we can merge"

	git-update-ref -m "initial pull" HEAD "$rh" "" &&
	git-read-tree --reset -u HEAD
	exit

else
	# We are invoked directly as the first-class UI.
	head_arg=HEAD

	# All the rest are the commits being merged; prepare
	# the standard merge summary message to be appended to
	# the given message.  If remote is invalid we will die
	# later in the common codepath so we discard the error
	# in this loop.
	merge_name=$(for remote
		do
			merge_name "$remote"
		done | git-fmt-merge-msg
	)
	merge_msg="${merge_msg:+$merge_msg$LF$LF}$merge_name"
fi
head=$(git-rev-parse --verify "$head_arg"^0) || usage

# All the rest are remote heads
test "$#" = 0 && usage ;# we need at least one remote head.
set_reflog_action "merge $*"

remoteheads=
for remote
do
	remotehead=$(git-rev-parse --verify "$remote"^0 2>/dev/null) ||
	    die "$remote - not something we can merge"
	remoteheads="${remoteheads}$remotehead "
	eval GITHEAD_$remotehead='"$remote"'
	export GITHEAD_$remotehead
done
set x $remoteheads ; shift

case "$use_strategies" in
'')
	case "$#" in
	1)
		var="`git-config --get pull.twohead`"
		if test -n "$var"
		then
			use_strategies="$var"
		else
			use_strategies="$default_twohead_strategies"
		fi ;;
	*)
		var="`git-config --get pull.octopus`"
		if test -n "$var"
		then
			use_strategies="$var"
		else
			use_strategies="$default_octopus_strategies"
		fi ;;
	esac
	;;
esac

for s in $use_strategies
do
	for nt in $no_trivial_merge_strategies
	do
		case " $s " in
		*" $nt "*)
			index_merge=f
			break
			;;
		esac
	done
done

case "$#" in
1)
	common=$(git-merge-base --all $head "$@")
	;;
*)
	common=$(git-show-branch --merge-base $head "$@")
	;;
esac
echo "$head" >"$GIT_DIR/ORIG_HEAD"

case "$index_merge,$#,$common,$no_commit" in
f,*)
	# We've been told not to try anything clever.  Skip to real merge.
	;;
?,*,'',*)
	# No common ancestors found. We need a real merge.
	;;
?,1,"$1",*)
	# If head can reach all the merge then we are up to date.
	# but first the most common case of merging one remote.
	finish_up_to_date "Already up-to-date."
	exit 0
	;;
?,1,"$head",*)
	# Again the most common case of merging one remote.
	echo "Updating $(git-rev-parse --short $head)..$(git-rev-parse --short $1)"
	git-update-index --refresh 2>/dev/null
	msg="Fast forward"
	if test -n "$have_message"
	then
		msg="$msg (no commit created; -m option ignored)"
	fi
	new_head=$(git-rev-parse --verify "$1^0") &&
	git-read-tree -v -m -u --exclude-per-directory=.gitignore $head "$new_head" &&
	finish "$new_head" "$msg" || exit
	dropsave
	exit 0
	;;
?,1,?*"$LF"?*,*)
	# We are not doing octopus and not fast forward.  Need a
	# real merge.
	;;
?,1,*,)
	# We are not doing octopus, not fast forward, and have only
	# one common.
	git-update-index --refresh 2>/dev/null
	case " $use_strategies " in
	*' recursive '*|*' recur '*)
		: run merge later
		;;
	*)
		# See if it is really trivial.
		git var GIT_COMMITTER_IDENT >/dev/null || exit
		echo "Trying really trivial in-index merge..."
		if git-read-tree --trivial -m -u -v $common $head "$1" &&
		   result_tree=$(git-write-tree)
		then
			echo "Wonderful."
			result_commit=$(
				echo "$merge_msg" |
				git-commit-tree $result_tree -p HEAD -p "$1"
			) || exit
			finish "$result_commit" "In-index merge"
			dropsave
			exit 0
		fi
		echo "Nope."
	esac
	;;
*)
	# An octopus.  If we can reach all the remote we are up to date.
	up_to_date=t
	for remote
	do
		common_one=$(git-merge-base --all $head $remote)
		if test "$common_one" != "$remote"
		then
			up_to_date=f
			break
		fi
	done
	if test "$up_to_date" = t
	then
		finish_up_to_date "Already up-to-date. Yeeah!"
		exit 0
	fi
	;;
esac

# We are going to make a new commit.
git var GIT_COMMITTER_IDENT >/dev/null || exit

# At this point, we need a real merge.  No matter what strategy
# we use, it would operate on the index, possibly affecting the
# working tree, and when resolved cleanly, have the desired tree
# in the index -- this means that the index must be in sync with
# the $head commit.  The strategies are responsible to ensure this.

case "$use_strategies" in
?*' '?*)
    # Stash away the local changes so that we can try more than one.
    savestate
    single_strategy=no
    ;;
*)
    rm -f "$GIT_DIR/MERGE_SAVE"
    single_strategy=yes
    ;;
esac

result_tree= best_cnt=-1 best_strategy= wt_strategy=
merge_was_ok=
for strategy in $use_strategies
do
    test "$wt_strategy" = '' || {
	echo "Rewinding the tree to pristine..."
	restorestate
    }
    case "$single_strategy" in
    no)
	echo "Trying merge strategy $strategy..."
	;;
    esac

    # Remember which strategy left the state in the working tree
    wt_strategy=$strategy

    git-merge-$strategy $common -- "$head_arg" "$@"
    exit=$?
    if test "$no_commit" = t && test "$exit" = 0
    then
        merge_was_ok=t
	exit=1 ;# pretend it left conflicts.
    fi

    test "$exit" = 0 || {

	# The backend exits with 1 when conflicts are left to be resolved,
	# with 2 when it does not handle the given merge at all.

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
    parents=$(git-show-branch --independent "$head" "$@" | sed -e 's/^/-p /')
    result_commit=$(echo "$merge_msg" | git-commit-tree $result_tree $parents) || exit
    finish "$result_commit" "Merge made by $wt_strategy."
    dropsave
    exit 0
fi

# Pick the result from the best strategy and have the user fix it up.
case "$best_strategy" in
'')
	restorestate
	case "$use_strategies" in
	?*' '?*)
		echo >&2 "No merge strategy handled the merge."
		;;
	*)
		echo >&2 "Merge with strategy $use_strategies failed."
		;;
	esac
	exit 2
	;;
"$wt_strategy")
	# We already have its result in the working tree.
	;;
*)
	echo "Rewinding the tree to pristine..."
	restorestate
	echo "Using the $best_strategy to prepare resolving by hand."
	git-merge-$best_strategy $common -- "$head_arg" "$@"
	;;
esac

if test "$squash" = t
then
	finish
else
	for remote
	do
		echo $remote
	done >"$GIT_DIR/MERGE_HEAD"
	echo "$merge_msg" >"$GIT_DIR/MERGE_MSG"
fi

if test "$merge_was_ok" = t
then
	echo >&2 \
	"Automatic merge went well; stopped before committing as requested"
	exit 0
else
	{
	    echo '
Conflicts:
'
		git ls-files --unmerged |
		sed -e 's/^[^	]*	/	/' |
		uniq
	} >>"$GIT_DIR/MERGE_MSG"
	if test -d "$GIT_DIR/rr-cache"
	then
		git-rerere
	fi
	die "Automatic merge failed; fix conflicts and then commit the result."
fi
