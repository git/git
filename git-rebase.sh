#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

USAGE='[--interactive | -i] [-v] [--force-rebase | -f] [--no-ff] [--onto <newbase>] (<upstream>|--root) [<branch>] [--quiet | -q]'
LONG_USAGE='git-rebase replaces <branch> with a new branch of the
same name.  When the --onto option is provided the new branch starts
out with a HEAD equal to <newbase>, otherwise it is equal to <upstream>
It then attempts to create a new commit for each commit from the original
<branch> that does not exist in the <upstream> branch.

It is possible that a merge failure will prevent this process from being
completely automatic.  You will have to resolve any such merge failure
and run git rebase --continue.  Another option is to bypass the commit
that caused the merge failure with git rebase --skip.  To restore the
original <branch> and remove the .git/rebase-apply working files, use the
command git rebase --abort instead.

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

LF='
'
OK_TO_SKIP_PRE_REBASE=
RESOLVEMSG="
When you have resolved this problem run \"git rebase --continue\".
If you would prefer to skip this patch, instead run \"git rebase --skip\".
To restore the original branch and stop rebasing run \"git rebase --abort\".
"
unset newbase
strategy=recursive
do_merge=
dotest="$GIT_DIR"/rebase-merge
prec=4
verbose=
diffstat=$(git config --bool rebase.stat)
git_am_opt=
rebase_root=
force_rebase=
allow_rerere_autoupdate=

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
	if ! git diff-index --quiet --ignore-submodules HEAD --
	then
		if ! git commit --no-verify -C "$cmt"
		then
			echo "Commit failed, please do not call \"git commit\""
			echo "directly, but instead do one of the following: "
			die "$RESOLVEMSG"
		fi
		if test -z "$GIT_QUIET"
		then
			printf "Committed: %0${prec}d " $msgnum
		fi
		echo "$cmt $(git rev-parse HEAD^0)" >> "$dotest/rewritten"
	else
		if test -z "$GIT_QUIET"
		then
			printf "Already applied: %0${prec}d " $msgnum
		fi
	fi
	test -z "$GIT_QUIET" &&
	GIT_PAGER='' git log --format=%s -1 "$cmt"

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
	if test -n "$GIT_QUIET"
	then
		export GIT_MERGE_VERBOSITY=1
	fi
	git-merge-$strategy "$cmt^" -- "$hd" "$cmt"
	rv=$?
	case "$rv" in
	0)
		unset GITHEAD_$cmt GITHEAD_$hd
		return
		;;
	1)
		git rerere $allow_rerere_autoupdate
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
	git notes copy --for-rewrite=rebase < "$dotest"/rewritten
	if test -x "$GIT_DIR"/hooks/post-rewrite &&
		test -s "$dotest"/rewritten; then
		"$GIT_DIR"/hooks/post-rewrite rebase < "$dotest"/rewritten
	fi
	rm -r "$dotest"
	say All done.
}

is_interactive () {
	while test $# != 0
	do
		case "$1" in
			-i|--interactive)
				interactive_rebase=explicit
				break
			;;
			-p|--preserve-merges)
				interactive_rebase=implied
			;;
		esac
		shift
	done

	if [ "$interactive_rebase" = implied ]; then
		GIT_EDITOR=:
		export GIT_EDITOR
	fi

	test -n "$interactive_rebase" || test -f "$dotest"/interactive
}

run_pre_rebase_hook () {
	if test -z "$OK_TO_SKIP_PRE_REBASE" &&
	   test -x "$GIT_DIR/hooks/pre-rebase"
	then
		"$GIT_DIR/hooks/pre-rebase" ${1+"$@"} ||
		die "The pre-rebase hook refused to rebase."
	fi
}

test -f "$GIT_DIR"/rebase-apply/applying &&
	die 'It looks like git-am is in progress. Cannot rebase.'

is_interactive "$@" && exec git-rebase--interactive "$@"

while test $# != 0
do
	case "$1" in
	--no-verify)
		OK_TO_SKIP_PRE_REBASE=yes
		;;
	--continue)
		test -d "$dotest" -o -d "$GIT_DIR"/rebase-apply ||
			die "No rebase in progress?"

		git update-index --ignore-submodules --refresh &&
		git diff-files --quiet --ignore-submodules || {
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
			GIT_QUIET=$(cat "$dotest/quiet")
			continue_merge
			while test "$msgnum" -le "$end"
			do
				call_merge "$msgnum"
				continue_merge
			done
			finish_rb_merge
			exit
		fi
		head_name=$(cat "$GIT_DIR"/rebase-apply/head-name) &&
		onto=$(cat "$GIT_DIR"/rebase-apply/onto) &&
		orig_head=$(cat "$GIT_DIR"/rebase-apply/orig-head) &&
		GIT_QUIET=$(cat "$GIT_DIR"/rebase-apply/quiet)
		git am --resolved --3way --resolvemsg="$RESOLVEMSG" &&
		move_to_original_branch
		exit
		;;
	--skip)
		test -d "$dotest" -o -d "$GIT_DIR"/rebase-apply ||
			die "No rebase in progress?"

		git reset --hard HEAD || exit $?
		if test -d "$dotest"
		then
			git rerere clear
			prev_head=$(cat "$dotest/prev_head")
			end=$(cat "$dotest/end")
			msgnum=$(cat "$dotest/msgnum")
			msgnum=$(($msgnum + 1))
			onto=$(cat "$dotest/onto")
			GIT_QUIET=$(cat "$dotest/quiet")
			while test "$msgnum" -le "$end"
			do
				call_merge "$msgnum"
				continue_merge
			done
			finish_rb_merge
			exit
		fi
		head_name=$(cat "$GIT_DIR"/rebase-apply/head-name) &&
		onto=$(cat "$GIT_DIR"/rebase-apply/onto) &&
		orig_head=$(cat "$GIT_DIR"/rebase-apply/orig-head) &&
		GIT_QUIET=$(cat "$GIT_DIR"/rebase-apply/quiet)
		git am -3 --skip --resolvemsg="$RESOLVEMSG" &&
		move_to_original_branch
		exit
		;;
	--abort)
		test -d "$dotest" -o -d "$GIT_DIR"/rebase-apply ||
			die "No rebase in progress?"

		git rerere clear
		if test -d "$dotest"
		then
			GIT_QUIET=$(cat "$dotest/quiet")
			move_to_original_branch
		else
			dotest="$GIT_DIR"/rebase-apply
			GIT_QUIET=$(cat "$dotest/quiet")
			move_to_original_branch
		fi
		git reset --hard $(cat "$dotest/orig-head")
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
	-n|--no-stat)
		diffstat=
		;;
	--stat)
		diffstat=t
		;;
	-v|--verbose)
		verbose=t
		diffstat=t
		GIT_QUIET=
		;;
	-q|--quiet)
		GIT_QUIET=t
		git_am_opt="$git_am_opt -q"
		verbose=
		diffstat=
		;;
	--whitespace=*)
		git_am_opt="$git_am_opt $1"
		case "$1" in
		--whitespace=fix|--whitespace=strip)
			force_rebase=t
			;;
		esac
		;;
	--ignore-whitespace)
		git_am_opt="$git_am_opt $1"
		;;
	--committer-date-is-author-date|--ignore-date)
		git_am_opt="$git_am_opt $1"
		force_rebase=t
		;;
	-C*)
		git_am_opt="$git_am_opt $1"
		;;
	--root)
		rebase_root=t
		;;
	-f|--f|--fo|--for|--forc|--force|--force-r|--force-re|--force-reb|--force-reba|--force-rebas|--force-rebase|--no-ff)
		force_rebase=t
		;;
	--rerere-autoupdate|--no-rerere-autoupdate)
		allow_rerere_autoupdate="$1"
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
test $# -gt 2 && usage

if test $# -eq 0 && test -z "$rebase_root"
then
	test -d "$dotest" -o -d "$GIT_DIR"/rebase-apply || usage
	test -d "$dotest" -o -f "$GIT_DIR"/rebase-apply/rebasing &&
		die 'A rebase is in progress, try --continue, --skip or --abort.'
fi

# Make sure we do not have $GIT_DIR/rebase-apply
if test -z "$do_merge"
then
	if mkdir "$GIT_DIR"/rebase-apply 2>/dev/null
	then
		rmdir "$GIT_DIR"/rebase-apply
	else
		echo >&2 '
It seems that I cannot create a rebase-apply directory, and
I wonder if you are in the middle of patch application or another
rebase.  If that is not the case, please
	rm -fr '"$GIT_DIR"'/rebase-apply
and run me again.  I am stopping in case you still have something
valuable there.'
		exit 1
	fi
else
	if test -d "$dotest"
	then
		die "previous rebase directory $dotest still exists." \
			'Try git rebase (--continue | --abort | --skip)'
	fi
fi

# The tree must be really really clean.
if ! git update-index --ignore-submodules --refresh > /dev/null; then
	echo >&2 "cannot rebase: you have unstaged changes"
	git diff-files --name-status -r --ignore-submodules -- >&2
	exit 1
fi
diff=$(git diff-index --cached --name-status -r --ignore-submodules HEAD --)
case "$diff" in
?*)	echo >&2 "cannot rebase: your index contains uncommitted changes"
	echo >&2 "$diff"
	exit 1
	;;
esac

if test -z "$rebase_root"
then
	# The upstream head must be given.  Make sure it is valid.
	upstream_name="$1"
	shift
	upstream=`git rev-parse --verify "${upstream_name}^0"` ||
	die "invalid upstream $upstream_name"
	unset root_flag
	upstream_arg="$upstream_name"
else
	test -z "$newbase" && die "--root must be used with --onto"
	unset upstream_name
	unset upstream
	root_flag="--root"
	upstream_arg="$root_flag"
fi

# Make sure the branch to rebase onto is valid.
onto_name=${newbase-"$upstream_name"}
case "$onto_name" in
*...*)
	if	left=${onto_name%...*} right=${onto_name#*...} &&
		onto=$(git merge-base --all ${left:-HEAD} ${right:-HEAD})
	then
		case "$onto" in
		?*"$LF"?*)
			die "$onto_name: there are more than one merge bases"
			;;
		'')
			die "$onto_name: there is no merge base"
			;;
		esac
	else
		die "$onto_name: there is no merge base"
	fi
	;;
*)
	onto=$(git rev-parse --verify "${onto_name}^0") || exit
	;;
esac

# If a hook exists, give it a chance to interrupt
run_pre_rebase_hook "$upstream_arg" "$@"

# If the branch to rebase is given, that is the branch we will rebase
# $branch_name -- branch being rebased, or HEAD (already detached)
# $orig_head -- commit object name of tip of the branch before rebasing
# $head_name -- refs/heads/<that-branch> or "detached HEAD"
switch_to=
case "$#" in
1)
	# Is it "rebase other $branchname" or "rebase other $commit"?
	branch_name="$1"
	switch_to="$1"

	if git show-ref --verify --quiet -- "refs/heads/$1" &&
	   branch=$(git rev-parse -q --verify "refs/heads/$1")
	then
		head_name="refs/heads/$1"
	elif branch=$(git rev-parse -q --verify "$1")
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

# Now we are rebasing commits $upstream..$branch (or with --root,
# everything leading up to $branch) on top of $onto

# Check if we are already based on $onto with linear history,
# but this should be done only when upstream and onto are the same.
mb=$(git merge-base "$onto" "$branch")
if test "$upstream" = "$onto" && test "$mb" = "$onto" &&
	# linear history?
	! (git rev-list --parents "$onto".."$branch" | sane_grep " .* ") > /dev/null
then
	if test -z "$force_rebase"
	then
		# Lazily switch to the target branch if needed...
		test -z "$switch_to" || git checkout "$switch_to"
		say "Current branch $branch_name is up to date."
		exit 0
	else
		say "Current branch $branch_name is up to date, rebase forced."
	fi
fi

# Detach HEAD and reset the tree
say "First, rewinding head to replay your work on top of it..."
git checkout -q "$onto^0" || die "could not detach HEAD"
git update-ref ORIG_HEAD $branch

if test -n "$diffstat"
then
	if test -n "$verbose"
	then
		echo "Changes from $mb to $onto:"
	fi
	# We want color (if set), but no pager
	GIT_PAGER='' git diff --stat --summary "$mb" "$onto"
fi

# If the $onto is a proper descendant of the tip of the branch, then
# we just fast-forwarded.
if test "$mb" = "$branch"
then
	say "Fast-forwarded $branch_name to $onto_name."
	move_to_original_branch
	exit 0
fi

if test -n "$rebase_root"
then
	revisions="$onto..$orig_head"
else
	revisions="$upstream..$orig_head"
fi

if test -z "$do_merge"
then
	git format-patch -k --stdout --full-index --ignore-if-in-upstream \
		--no-renames $root_flag "$revisions" |
	git am $git_am_opt --rebasing --resolvemsg="$RESOLVEMSG" &&
	move_to_original_branch
	ret=$?
	test 0 != $ret -a -d "$GIT_DIR"/rebase-apply &&
		echo $head_name > "$GIT_DIR"/rebase-apply/head-name &&
		echo $onto > "$GIT_DIR"/rebase-apply/onto &&
		echo $orig_head > "$GIT_DIR"/rebase-apply/orig-head &&
		echo "$GIT_QUIET" > "$GIT_DIR"/rebase-apply/quiet
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
echo "$GIT_QUIET" > "$dotest/quiet"

msgnum=0
for cmt in `git rev-list --reverse --no-merges "$revisions"`
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
