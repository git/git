#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

OPTIONS_KEEPDASHDASH=
OPTIONS_SPEC="\
git merge [options] <remote>...
git merge [options] <msg> HEAD <remote>
--
stat                 show a diffstat at the end of the merge
n                    don't show a diffstat at the end of the merge
summary              (synonym to --stat)
log                  add list of one-line log to merge commit message
squash               create a single commit instead of doing a merge
commit               perform a commit if the merge succeeds (default)
ff                   allow fast-forward (default)
ff-only              abort if fast-forward is not possible
rerere-autoupdate    update index with any reused conflict resolution
s,strategy=          merge strategy to use
X=                   option for selected merge strategy
m,message=           message to be used for the merge commit (if any)
"

SUBDIRECTORY_OK=Yes
. git-sh-setup
require_work_tree
cd_to_toplevel

test -z "$(git ls-files -u)" ||
	die "Merge is not possible because you have unmerged files."

! test -e "$GIT_DIR/MERGE_HEAD" ||
	die 'You have not concluded your merge (MERGE_HEAD exists).'

LF='
'

all_strategies='recur recursive octopus resolve stupid ours subtree'
all_strategies="$all_strategies recursive-ours recursive-theirs"
not_strategies='base file index tree'
default_twohead_strategies='recursive'
default_octopus_strategies='octopus'
no_fast_forward_strategies='subtree ours'
no_trivial_strategies='recursive recur subtree ours recursive-ours recursive-theirs'
use_strategies=
xopt=

allow_fast_forward=t
fast_forward_only=
allow_trivial_merge=t
squash= no_commit= log_arg= rr_arg=

dropsave() {
	rm -f -- "$GIT_DIR/MERGE_HEAD" "$GIT_DIR/MERGE_MSG" \
		 "$GIT_DIR/MERGE_STASH" "$GIT_DIR/MERGE_MODE" || exit 1
}

savestate() {
	# Stash away any local modifications.
	git stash create >"$GIT_DIR/MERGE_STASH"
}

restorestate() {
        if test -f "$GIT_DIR/MERGE_STASH"
	then
		git reset --hard $head >/dev/null
		git stash apply $(cat "$GIT_DIR/MERGE_STASH")
		git update-index --refresh >/dev/null
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
	git log --no-merges --pretty=medium ^"$head" $remoteheads
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
			git update-ref -m "$rlogm" HEAD "$1" "$head" || exit 1
			git gc --auto
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
			GIT_PAGER='' git diff --stat --summary -M "$head" "$1"
		fi
		;;
	esac

	# Run a post-merge hook
        if test -x "$GIT_DIR"/hooks/post-merge
        then
	    case "$squash" in
	    t)
                "$GIT_DIR"/hooks/post-merge 1
		;;
	    '')
                "$GIT_DIR"/hooks/post-merge 0
		;;
	    esac
        fi
}

merge_name () {
	remote="$1"
	rh=$(git rev-parse --verify "$remote^0" 2>/dev/null) || return
	if truname=$(expr "$remote" : '\(.*\)~[0-9]*$') &&
		git show-ref -q --verify "refs/heads/$truname" 2>/dev/null
	then
		echo "$rh		branch '$truname' (early part) of ."
		return
	fi
	if found_ref=$(git rev-parse --symbolic-full-name --verify \
							"$remote" 2>/dev/null)
	then
		expanded=$(git check-ref-format --branch "$remote") ||
			exit
		if test "${found_ref#refs/heads/}" != "$found_ref"
		then
			echo "$rh		branch '$expanded' of ."
			return
		elif test "${found_ref#refs/remotes/}" != "$found_ref"
		then
			echo "$rh		remote branch '$expanded' of ."
			return
		fi
	fi
	if test "$remote" = "FETCH_HEAD" && test -r "$GIT_DIR/FETCH_HEAD"
	then
		sed -e 's/	not-for-merge	/		/' -e 1q \
			"$GIT_DIR/FETCH_HEAD"
		return
	fi
	echo "$rh		commit '$remote'"
}

parse_config () {
	while test $# != 0; do
		case "$1" in
		-n|--no-stat|--no-summary)
			show_diffstat=false ;;
		--stat|--summary)
			show_diffstat=t ;;
		--log|--no-log)
			log_arg=$1 ;;
		--squash)
			test "$allow_fast_forward" = t ||
				die "You cannot combine --squash with --no-ff."
			squash=t no_commit=t ;;
		--no-squash)
			squash= no_commit= ;;
		--commit)
			no_commit= ;;
		--no-commit)
			no_commit=t ;;
		--ff)
			allow_fast_forward=t ;;
		--no-ff)
			test "$squash" != t ||
				die "You cannot combine --squash with --no-ff."
			test "$fast_forward_only" != t ||
				die "You cannot combine --ff-only with --no-ff."
			allow_fast_forward=f ;;
		--ff-only)
			test "$allow_fast_forward" != f ||
				die "You cannot combine --ff-only with --no-ff."
			fast_forward_only=t ;;
		--rerere-autoupdate|--no-rerere-autoupdate)
			rr_arg=$1 ;;
		-s|--strategy)
			shift
			case " $all_strategies " in
			*" $1 "*)
				use_strategies="$use_strategies$1 "
				;;
			*)
				case " $not_strategies " in
				*" $1 "*)
					false
				esac &&
				type "git-merge-$1" >/dev/null 2>&1 ||
					die "available strategies are: $all_strategies"
				use_strategies="$use_strategies$1 "
				;;
			esac
			;;
		-X)
			shift
			xopt="${xopt:+$xopt }$(git rev-parse --sq-quote "--$1")"
			;;
		-m|--message)
			shift
			merge_msg="$1"
			have_message=t
			;;
		--)
			shift
			break ;;
		*)	usage ;;
		esac
		shift
	done
	args_left=$#
}

test $# != 0 || usage

have_message=

if branch=$(git-symbolic-ref -q HEAD)
then
	mergeopts=$(git config "branch.${branch#refs/heads/}.mergeoptions")
	if test -n "$mergeopts"
	then
		parse_config $mergeopts --
	fi
fi

parse_config "$@"
while test $args_left -lt $#; do shift; done

if test -z "$show_diffstat"; then
    test "$(git config --bool merge.diffstat)" = false && show_diffstat=false
    test "$(git config --bool merge.stat)" = false && show_diffstat=false
    test -z "$show_diffstat" && show_diffstat=t
fi

# This could be traditional "merge <msg> HEAD <commit>..."  and the
# way we can tell it is to see if the second token is HEAD, but some
# people might have misused the interface and used a commit-ish that
# is the same as HEAD there instead.  Traditional format never would
# have "-m" so it is an additional safety measure to check for it.

if test -z "$have_message" &&
	second_token=$(git rev-parse --verify "$2^0" 2>/dev/null) &&
	head_commit=$(git rev-parse --verify "HEAD" 2>/dev/null) &&
	test "$second_token" = "$head_commit"
then
	merge_msg="$1"
	shift
	head_arg="$1"
	shift
elif ! git rev-parse --verify HEAD >/dev/null 2>&1
then
	# If the merged head is a valid one there is no reason to
	# forbid "git merge" into a branch yet to be born.  We do
	# the same for "git pull".
	if test 1 -ne $#
	then
		echo >&2 "Can merge only exactly one commit into empty head"
		exit 1
	fi

	test "$squash" != t ||
		die "Squash commit into empty head not supported yet"
	test "$allow_fast_forward" = t ||
		die "Non-fast-forward into an empty head does not make sense"
	rh=$(git rev-parse --verify "$1^0") ||
		die "$1 - not something we can merge"

	git update-ref -m "initial pull" HEAD "$rh" "" &&
	git read-tree --reset -u HEAD
	exit

else
	# We are invoked directly as the first-class UI.
	head_arg=HEAD

	# All the rest are the commits being merged; prepare
	# the standard merge summary message to be appended to
	# the given message.  If remote is invalid we will die
	# later in the common codepath so we discard the error
	# in this loop.
	merge_msg="$(
		for remote
		do
			merge_name "$remote"
		done |
		if test "$have_message" = t
		then
			git fmt-merge-msg -m "$merge_msg" $log_arg
		else
			git fmt-merge-msg $log_arg
		fi
	)"
fi
head=$(git rev-parse --verify "$head_arg"^0) || usage

# All the rest are remote heads
test "$#" = 0 && usage ;# we need at least one remote head.
set_reflog_action "merge $*"

remoteheads=
for remote
do
	remotehead=$(git rev-parse --verify "$remote"^0 2>/dev/null) ||
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
		var="$(git config --get pull.twohead)"
		if test -n "$var"
		then
			use_strategies="$var"
		else
			use_strategies="$default_twohead_strategies"
		fi ;;
	*)
		var="$(git config --get pull.octopus)"
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
	for ss in $no_fast_forward_strategies
	do
		case " $s " in
		*" $ss "*)
			allow_fast_forward=f
			break
			;;
		esac
	done
	for ss in $no_trivial_strategies
	do
		case " $s " in
		*" $ss "*)
			allow_trivial_merge=f
			break
			;;
		esac
	done
done

case "$#" in
1)
	common=$(git merge-base --all $head "$@")
	;;
*)
	common=$(git merge-base --all --octopus $head "$@")
	;;
esac
echo "$head" >"$GIT_DIR/ORIG_HEAD"

case "$allow_fast_forward,$#,$common,$no_commit" in
?,*,'',*)
	# No common ancestors found. We need a real merge.
	;;
?,1,"$1",*)
	# If head can reach all the merge then we are up to date.
	# but first the most common case of merging one remote.
	finish_up_to_date "Already up-to-date."
	exit 0
	;;
t,1,"$head",*)
	# Again the most common case of merging one remote.
	echo "Updating $(git rev-parse --short $head)..$(git rev-parse --short $1)"
	git update-index --refresh 2>/dev/null
	msg="Fast-forward"
	if test -n "$have_message"
	then
		msg="$msg (no commit created; -m option ignored)"
	fi
	new_head=$(git rev-parse --verify "$1^0") &&
	git read-tree -v -m -u --exclude-per-directory=.gitignore $head "$new_head" &&
	finish "$new_head" "$msg" || exit
	dropsave
	exit 0
	;;
?,1,?*"$LF"?*,*)
	# We are not doing octopus and not fast-forward.  Need a
	# real merge.
	;;
?,1,*,)
	# We are not doing octopus, not fast-forward, and have only
	# one common.
	git update-index --refresh 2>/dev/null
	case "$allow_trivial_merge,$fast_forward_only" in
	t,)
		# See if it is really trivial.
		git var GIT_COMMITTER_IDENT >/dev/null || exit
		echo "Trying really trivial in-index merge..."
		if git read-tree --trivial -m -u -v $common $head "$1" &&
		   result_tree=$(git write-tree)
		then
			echo "Wonderful."
			result_commit=$(
				printf '%s\n' "$merge_msg" |
				git commit-tree $result_tree -p HEAD -p "$1"
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
		common_one=$(git merge-base --all $head $remote)
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

if test "$fast_forward_only" = t
then
	die "Not possible to fast-forward, aborting."
fi

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
    rm -f "$GIT_DIR/MERGE_STASH"
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

    eval 'git-merge-$strategy '"$xopt"' $common -- "$head_arg" "$@"'
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
		git diff-files --name-only
		git ls-files --unmerged
	    } | wc -l`
	    if test $best_cnt -le 0 || test $cnt -le $best_cnt
	    then
		best_strategy=$strategy
		best_cnt=$cnt
	    fi
	fi
	continue
    }

    # Automerge succeeded.
    result_tree=$(git write-tree) && break
done

# If we have a resulting tree, that means the strategy module
# auto resolved the merge cleanly.
if test '' != "$result_tree"
then
    if test "$allow_fast_forward" = "t"
    then
	parents=$(git merge-base --independent "$head" "$@")
    else
	parents=$(git rev-parse "$head" "$@")
    fi
    parents=$(echo "$parents" | sed -e 's/^/-p /')
    result_commit=$(printf '%s\n' "$merge_msg" | git commit-tree $result_tree $parents) || exit
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
	printf '%s\n' "$merge_msg" >"$GIT_DIR/MERGE_MSG" ||
		die "Could not write to $GIT_DIR/MERGE_MSG"
	if test "$allow_fast_forward" != t
	then
		printf "%s" no-ff
	else
		:
	fi >"$GIT_DIR/MERGE_MODE" ||
		die "Could not write to $GIT_DIR/MERGE_MODE"
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
	git rerere $rr_arg
	die "Automatic merge failed; fix conflicts and then commit the result."
fi
