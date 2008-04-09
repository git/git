#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

USAGE='[--interactive | -i] [-v] [--onto <newbase>] <upstream> [<branch>]'
LONG_USAGE='git-rebase replaces <branch> with a new branch of the
same name.  When the --onto option is provided the new branch starts
out with a HEAD equal to <newbase>, otherwise it is equal to <upstream>
It then attempts to create a new commit for each commit from the original
<branch> that does not exist in the <upstream> branch.

It is possible that a merge failure will prevent this process from being
completely automatic.  You will have to resolve any such merge failure
and run git rebase --continue.  Another option is to bypass the commit
that caused the merge failure with git rebase --skip.  To restore the
original <branch> and remove the .dotest working files, use the command
git rebase --abort instead.

Note that if <branch> is not specified on the command line, the
currently checked out branch is used.

Example:       git-rebase master~1 topic

        A---B---C topic                   A'\''--B'\''--C'\'' topic
       /                   -->           /
  D---E---F---G master          D---E---F---G master
'

SUBDIRECTORY_OK=Yes
OPTIONS_SPEC=
. git-sh-setup
set_reflog_action rebase
require_work_tree
cd_to_toplevel

RESOLVEMSG="
When you have resolved this problem run \"git rebase --continue\".
If you would prefer to skip this patch, instead run \"git rebase --skip\".
To restore the original branch and stop rebasing run \"git rebase --abort\".
"
unset newbase
strategy=recursive
do_merge=
dotest=$GIT_DIR/.dotest-merge
prec=4
verbose=
git_am_opt=

continue_merge () {
	test -n "$prev_head" || die "prev_head must be defined"
	test -d "$dotest" || die "$dotest directory does not exist"

	unmerged=$(git ls-files -u)
	if test -n "$unmerged"
	then
		echo "You still have unmerged paths in your index"
		echo "did you forget to use git add?"
		die "$RESOLVEMSG"
	fi

	cmt=`cat "$dotest/current"`
	if ! git diff-index --quiet HEAD --
	then
		if ! git commit --no-verify -C "$cmt"
		then
			echo "Commit failed, please do not call \"git commit\""
			echo "directly, but instead do one of the following: "
			die "$RESOLVEMSG"
		fi
		printf "Committed: %0${prec}d " $msgnum
	else
		printf "Already applied: %0${prec}d " $msgnum
	fi
	git rev-list --pretty=oneline -1 "$cmt" | sed -e 's/^[^ ]* //'

	prev_head=`git rev-parse HEAD^0`
	# save the resulting commit so we can read-tree on it later
	echo "$prev_head" > "$dotest/prev_head"

	# onto the next patch:
	msgnum=$(($msgnum + 1))
	echo "$msgnum" >"$dotest/msgnum"
}

call_merge () {
	cmt="$(cat "$dotest/cmt.$1")"
	echo "$cmt" > "$dotest/current"
	hd=$(git rev-parse --verify HEAD)
	cmt_name=$(git symbolic-ref HEAD 2> /dev/null || echo HEAD)
	msgnum=$(cat "$dotest/msgnum")
	end=$(cat "$dotest/end")
	eval GITHEAD_$cmt='"${cmt_name##refs/heads/}~$(($end - $msgnum))"'
	eval GITHEAD_$hd='$(cat "$dotest/onto_name")'
	export GITHEAD_$cmt GITHEAD_$hd
	git-merge-$strategy "$cmt^" -- "$hd" "$cmt"
	rv=$?
	case "$rv" in
	0)
		unset GITHEAD_$cmt GITHEAD_$hd
		return
		;;
	1)
		git rerere
		die "$RESOLVEMSG"
		;;
	2)
		echo "Strategy: $rv $strategy failed, try another" 1>&2
		die "$RESOLVEMSG"
		;;
	*)
		die "Unknown exit code ($rv) from command:" \
			"git-merge-$strategy $cmt^ -- HEAD $cmt"
		;;
	esac
}

move_to_original_branch () {
	test -z "$head_name" &&
		head_name="$(cat "$dotest"/head-name)" &&
		onto="$(cat "$dotest"/onto)" &&
		orig_head="$(cat "$dotest"/orig-head)"
	case "$head_name" in
	refs/*)
		message="rebase finished: $head_name onto $onto"
		git update-ref -m "$message" \
			$head_name $(git rev-parse HEAD) $orig_head &&
		git symbolic-ref HEAD $head_name ||
		die "Could not move back to $head_name"
		;;
	esac
}

finish_rb_merge () {
	move_to_original_branch
	rm -r "$dotest"
	echo "All done."
}

is_interactive () {
	test -f "$dotest"/interactive ||
	while :; do case $#,"$1" in 0,|*,-i|*,--interactive) break ;; esac
		shift
	done && test -n "$1"
}

is_interactive "$@" && exec git-rebase--interactive "$@"

while test $# != 0
do
	case "$1" in
	--continue)
		git diff-files --quiet || {
			echo "You must edit all merge conflicts and then"
			echo "mark them as resolved using git add"
			exit 1
		}
		if test -d "$dotest"
		then
			prev_head=$(cat "$dotest/prev_head")
			end=$(cat "$dotest/end")
			msgnum=$(cat "$dotest/msgnum")
			onto=$(cat "$dotest/onto")
			continue_merge
			while test "$msgnum" -le "$end"
			do
				call_merge "$msgnum"
				continue_merge
			done
			finish_rb_merge
			exit
		fi
		head_name=$(cat .dotest/head-name) &&
		onto=$(cat .dotest/onto) &&
		orig_head=$(cat .dotest/orig-head) &&
		git am --resolved --3way --resolvemsg="$RESOLVEMSG" &&
		move_to_original_branch
		exit
		;;
	--skip)
		git reset --hard HEAD || exit $?
		if test -d "$dotest"
		then
			git rerere clear
			prev_head=$(cat "$dotest/prev_head")
			end=$(cat "$dotest/end")
			msgnum=$(cat "$dotest/msgnum")
			msgnum=$(($msgnum + 1))
			onto=$(cat "$dotest/onto")
			while test "$msgnum" -le "$end"
			do
				call_merge "$msgnum"
				continue_merge
			done
			finish_rb_merge
			exit
		fi
		head_name=$(cat .dotest/head-name) &&
		onto=$(cat .dotest/onto) &&
		orig_head=$(cat .dotest/orig-head) &&
		git am -3 --skip --resolvemsg="$RESOLVEMSG" &&
		move_to_original_branch
		exit
		;;
	--abort)
		git rerere clear
		if test -d "$dotest"
		then
			move_to_original_branch
		elif test -d .dotest
		then
			dotest=.dotest
			move_to_original_branch
		else
			die "No rebase in progress?"
		fi
		git reset --hard $(cat $dotest/orig-head)
		rm -r "$dotest"
		exit
		;;
	--onto)
		test 2 -le "$#" || usage
		newbase="$2"
		shift
		;;
	-M|-m|--m|--me|--mer|--merg|--merge)
		do_merge=t
		;;
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
		do_merge=t
		;;
	-v|--verbose)
		verbose=t
		;;
	--whitespace=*)
		git_am_opt="$git_am_opt $1"
		;;
	-C*)
		git_am_opt="$git_am_opt $1"
		;;
	-*)
		usage
		;;
	*)
		break
		;;
	esac
	shift
done

# Make sure we do not have .dotest
if test -z "$do_merge"
then
	if mkdir .dotest
	then
		rmdir .dotest
	else
		echo >&2 '
It seems that I cannot create a .dotest directory, and I wonder if you
are in the middle of patch application or another rebase.  If that is not
the case, please rm -fr .dotest and run me again.  I am stopping in case
you still have something valuable there.'
		exit 1
	fi
else
	if test -d "$dotest"
	then
		die "previous dotest directory $dotest still exists." \
			'try git-rebase < --continue | --abort >'
	fi
fi

# The tree must be really really clean.
git update-index --refresh || exit
diff=$(git diff-index --cached --name-status -r HEAD --)
case "$diff" in
?*)	echo "cannot rebase: your index is not up-to-date"
	echo "$diff"
	exit 1
	;;
esac

# The upstream head must be given.  Make sure it is valid.
upstream_name="$1"
upstream=`git rev-parse --verify "${upstream_name}^0"` ||
    die "invalid upstream $upstream_name"

# Make sure the branch to rebase onto is valid.
onto_name=${newbase-"$upstream_name"}
onto=$(git rev-parse --verify "${onto_name}^0") || exit

# If a hook exists, give it a chance to interrupt
if test -x "$GIT_DIR/hooks/pre-rebase"
then
	"$GIT_DIR/hooks/pre-rebase" ${1+"$@"} || {
		echo >&2 "The pre-rebase hook refused to rebase."
		exit 1
	}
fi

# If the branch to rebase is given, that is the branch we will rebase
# $branch_name -- branch being rebased, or HEAD (already detached)
# $orig_head -- commit object name of tip of the branch before rebasing
# $head_name -- refs/heads/<that-branch> or "detached HEAD"
switch_to=
case "$#" in
2)
	# Is it "rebase other $branchname" or "rebase other $commit"?
	branch_name="$2"
	switch_to="$2"

	if git show-ref --verify --quiet -- "refs/heads/$2" &&
	   branch=$(git rev-parse --verify "refs/heads/$2" 2>/dev/null)
	then
		head_name="refs/heads/$2"
	elif branch=$(git rev-parse --verify "$2" 2>/dev/null)
	then
		head_name="detached HEAD"
	else
		usage
	fi
	;;
*)
	# Do not need to switch branches, we are already on it.
	if branch_name=`git symbolic-ref -q HEAD`
	then
		head_name=$branch_name
		branch_name=`expr "z$branch_name" : 'zrefs/heads/\(.*\)'`
	else
		head_name="detached HEAD"
		branch_name=HEAD ;# detached
	fi
	branch=$(git rev-parse --verify "${branch_name}^0") || exit
	;;
esac
orig_head=$branch

# Now we are rebasing commits $upstream..$branch on top of $onto

# Check if we are already based on $onto with linear history,
# but this should be done only when upstream and onto are the same.
mb=$(git merge-base "$onto" "$branch")
if test "$upstream" = "$onto" && test "$mb" = "$onto" &&
	# linear history?
	! git rev-list --parents "$onto".."$branch" | grep " .* " > /dev/null
then
	# Lazily switch to the target branch if needed...
	test -z "$switch_to" || git checkout "$switch_to"
	echo >&2 "Current branch $branch_name is up to date."
	exit 0
fi

if test -n "$verbose"
then
	echo "Changes from $mb to $onto:"
	# We want color (if set), but no pager
	GIT_PAGER='' git diff --stat --summary "$mb" "$onto"
fi

# Detach HEAD and reset the tree
echo "First, rewinding head to replay your work on top of it..."
git checkout "$onto^0" >/dev/null 2>&1 ||
	die "could not detach HEAD"
# git reset --hard "$onto^0"

# If the $onto is a proper descendant of the tip of the branch, then
# we just fast forwarded.
if test "$mb" = "$branch"
then
	echo >&2 "Fast-forwarded $branch_name to $onto_name."
	move_to_original_branch
	exit 0
fi

if test -z "$do_merge"
then
	git format-patch -k --stdout --full-index --ignore-if-in-upstream \
		"$upstream..$orig_head" |
	git am $git_am_opt --rebasing --resolvemsg="$RESOLVEMSG" &&
	move_to_original_branch
	ret=$?
	test 0 != $ret -a -d .dotest &&
		echo $head_name > .dotest/head-name &&
		echo $onto > .dotest/onto &&
		echo $orig_head > .dotest/orig-head
	exit $ret
fi

# start doing a rebase with git-merge
# this is rename-aware if the recursive (default) strategy is used

mkdir -p "$dotest"
echo "$onto" > "$dotest/onto"
echo "$onto_name" > "$dotest/onto_name"
prev_head=$orig_head
echo "$prev_head" > "$dotest/prev_head"
echo "$orig_head" > "$dotest/orig-head"
echo "$head_name" > "$dotest/head-name"

msgnum=0
for cmt in `git rev-list --reverse --no-merges "$upstream..$orig_head"`
do
	msgnum=$(($msgnum + 1))
	echo "$cmt" > "$dotest/cmt.$msgnum"
done

echo 1 >"$dotest/msgnum"
echo $msgnum >"$dotest/end"

end=$msgnum
msgnum=1

while test "$msgnum" -le "$end"
do
	call_merge "$msgnum"
	continue_merge
done

finish_rb_merge
