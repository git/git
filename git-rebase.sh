#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

USAGE='[-v] [--onto <newbase>] <upstream> [<branch>]'
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
currently checked out branch is used.  You must be in the top
directory of your project to start (or continue) a rebase.

Example:       git-rebase master~1 topic

        A---B---C topic                   A'\''--B'\''--C'\'' topic
       /                   -->           /
  D---E---F---G master          D---E---F---G master
'

SUBDIRECTORY_OK=Yes
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

	unmerged=$(git-ls-files -u)
	if test -n "$unmerged"
	then
		echo "You still have unmerged paths in your index"
		echo "did you forget update-index?"
		die "$RESOLVEMSG"
	fi

	if test -n "`git-diff-index HEAD`"
	then
		if ! git-commit -C "`cat $dotest/current`"
		then
			echo "Commit failed, please do not call \"git commit\""
			echo "directly, but instead do one of the following: "
			die "$RESOLVEMSG"
		fi
		printf "Committed: %0${prec}d" $msgnum
	else
		printf "Already applied: %0${prec}d" $msgnum
	fi
	echo ' '`git-rev-list --pretty=oneline -1 HEAD | \
				sed 's/^[a-f0-9]\+ //'`

	prev_head=`git-rev-parse HEAD^0`
	# save the resulting commit so we can read-tree on it later
	echo "$prev_head" > "$dotest/prev_head"

	# onto the next patch:
	msgnum=$(($msgnum + 1))
	echo "$msgnum" >"$dotest/msgnum"
}

call_merge () {
	cmt="$(cat $dotest/cmt.$1)"
	echo "$cmt" > "$dotest/current"
	hd=$(git-rev-parse --verify HEAD)
	cmt_name=$(git-symbolic-ref HEAD)
	msgnum=$(cat $dotest/msgnum)
	end=$(cat $dotest/end)
	eval GITHEAD_$cmt='"${cmt_name##refs/heads/}~$(($end - $msgnum))"'
	eval GITHEAD_$hd='"$(cat $dotest/onto_name)"'
	export GITHEAD_$cmt GITHEAD_$hd
	git-merge-$strategy "$cmt^" -- "$hd" "$cmt"
	rv=$?
	case "$rv" in
	0)
		unset GITHEAD_$cmt GITHEAD_$hd
		return
		;;
	1)
		test -d "$GIT_DIR/rr-cache" && git-rerere
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

finish_rb_merge () {
	rm -r "$dotest"
	echo "All done."
}

while case "$#" in 0) break ;; esac
do
	case "$1" in
	--continue)
		diff=$(git-diff-files)
		case "$diff" in
		?*)	echo "You must edit all merge conflicts and then"
			echo "mark them as resolved using git update-index"
			exit 1
			;;
		esac
		if test -d "$dotest"
		then
			prev_head="`cat $dotest/prev_head`"
			end="`cat $dotest/end`"
			msgnum="`cat $dotest/msgnum`"
			onto="`cat $dotest/onto`"
			continue_merge
			while test "$msgnum" -le "$end"
			do
				call_merge "$msgnum"
				continue_merge
			done
			finish_rb_merge
			exit
		fi
		git am --resolved --3way --resolvemsg="$RESOLVEMSG"
		exit
		;;
	--skip)
		if test -d "$dotest"
		then
			if test -d "$GIT_DIR/rr-cache"
			then
				git-rerere clear
			fi
			prev_head="`cat $dotest/prev_head`"
			end="`cat $dotest/end`"
			msgnum="`cat $dotest/msgnum`"
			msgnum=$(($msgnum + 1))
			onto="`cat $dotest/onto`"
			while test "$msgnum" -le "$end"
			do
				call_merge "$msgnum"
				continue_merge
			done
			finish_rb_merge
			exit
		fi
		git am -3 --skip --resolvemsg="$RESOLVEMSG"
		exit
		;;
	--abort)
		if test -d "$GIT_DIR/rr-cache"
		then
			git-rerere clear
		fi
		if test -d "$dotest"
		then
			rm -r "$dotest"
		elif test -d .dotest
		then
			rm -r .dotest
		else
			die "No rebase in progress?"
		fi
		git reset --hard ORIG_HEAD
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
	-C*)
		git_am_opt=$1
		shift
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
git-update-index --refresh || exit
diff=$(git-diff-index --cached --name-status -r HEAD)
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
onto=$(git-rev-parse --verify "${onto_name}^0") || exit

# If a hook exists, give it a chance to interrupt
if test -x "$GIT_DIR/hooks/pre-rebase"
then
	"$GIT_DIR/hooks/pre-rebase" ${1+"$@"} || {
		echo >&2 "The pre-rebase hook refused to rebase."
		exit 1
	}
fi

# If the branch to rebase is given, first switch to it.
case "$#" in
2)
	branch_name="$2"
	git-checkout "$2" || usage
	;;
*)
	if branch_name=`git symbolic-ref -q HEAD`
	then
		branch_name=`expr "z$branch_name" : 'zrefs/heads/\(.*\)'`
	else
		branch_name=HEAD ;# detached
	fi
	;;
esac
branch=$(git-rev-parse --verify "${branch_name}^0") || exit

# Now we are rebasing commits $upstream..$branch on top of $onto

# Check if we are already based on $onto, but this should be
# done only when upstream and onto are the same.
mb=$(git-merge-base "$onto" "$branch")
if test "$upstream" = "$onto" && test "$mb" = "$onto"
then
	echo >&2 "Current branch $branch_name is up to date."
	exit 0
fi

if test -n "$verbose"
then
	echo "Changes from $mb to $onto:"
	git-diff-tree --stat --summary "$mb" "$onto"
fi

# Rewind the head to "$onto"; this saves our current head in ORIG_HEAD.
echo "First, rewinding head to replay your work on top of it..."
git-reset --hard "$onto"

# If the $onto is a proper descendant of the tip of the branch, then
# we just fast forwarded.
if test "$mb" = "$branch"
then
	echo >&2 "Fast-forwarded $branch_name to $onto_name."
	exit 0
fi

if test -z "$do_merge"
then
	git-format-patch -k --stdout --full-index --ignore-if-in-upstream "$upstream"..ORIG_HEAD |
	git am $git_am_opt --binary -3 -k --resolvemsg="$RESOLVEMSG"
	exit $?
fi

# start doing a rebase with git-merge
# this is rename-aware if the recursive (default) strategy is used

mkdir -p "$dotest"
echo "$onto" > "$dotest/onto"
echo "$onto_name" > "$dotest/onto_name"
prev_head=`git-rev-parse HEAD^0`
echo "$prev_head" > "$dotest/prev_head"

msgnum=0
for cmt in `git-rev-list --no-merges "$upstream"..ORIG_HEAD \
			| @@PERL@@ -e 'print reverse <>'`
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
