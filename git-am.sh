#!/bin/sh
#
# Copyright (c) 2005, 2006 Junio C Hamano

OPTIONS_KEEPDASHDASH=
OPTIONS_SPEC="\
git-am [options] <mbox>|<Maildir>...
git-am [options] --resolved
git-am [options] --skip
--
d,dotest=       use <dir> and not .dotest
i,interactive=  run interactively
b,binary        pass --allo-binary-replacement to git-apply
3,3way          allow fall back on 3way merging if needed
s,signoff       add a Signed-off-by line to the commit message
u,utf8          recode into utf8 (default)
k,keep          pass -k flagg to git-mailinfo
whitespace=     pass it through git-apply
C=              pass it through git-apply
p=              pass it through git-apply
resolvemsg=     override error message when patch failure occurs
r,resolved      to be used after a patch failure
skip            skip the current patch"

. git-sh-setup
set_reflog_action am
require_work_tree

git var GIT_COMMITTER_IDENT >/dev/null || exit

stop_here () {
    echo "$1" >"$dotest/next"
    exit 1
}

stop_here_user_resolve () {
    if [ -n "$resolvemsg" ]; then
	    printf '%s\n' "$resolvemsg"
	    stop_here $1
    fi
    cmdline=$(basename $0)
    if test '' != "$interactive"
    then
        cmdline="$cmdline -i"
    fi
    if test '' != "$threeway"
    then
        cmdline="$cmdline -3"
    fi
    if test '.dotest' != "$dotest"
    then
        cmdline="$cmdline -d=$dotest"
    fi
    echo "When you have resolved this problem run \"$cmdline --resolved\"."
    echo "If you would prefer to skip this patch, instead run \"$cmdline --skip\"."

    stop_here $1
}

go_next () {
	rm -f "$dotest/$msgnum" "$dotest/msg" "$dotest/msg-clean" \
		"$dotest/patch" "$dotest/info"
	echo "$next" >"$dotest/next"
	this=$next
}

cannot_fallback () {
	echo "$1"
	echo "Cannot fall back to three-way merge."
	exit 1
}

fall_back_3way () {
    O_OBJECT=`cd "$GIT_OBJECT_DIRECTORY" && pwd`

    rm -fr "$dotest"/patch-merge-*
    mkdir "$dotest/patch-merge-tmp-dir"

    # First see if the patch records the index info that we can use.
    git apply --build-fake-ancestor "$dotest/patch-merge-tmp-index" \
	"$dotest/patch" &&
    GIT_INDEX_FILE="$dotest/patch-merge-tmp-index" \
    git write-tree >"$dotest/patch-merge-base+" ||
    cannot_fallback "Repository lacks necessary blobs to fall back on 3-way merge."

    echo Using index info to reconstruct a base tree...
    if GIT_INDEX_FILE="$dotest/patch-merge-tmp-index" \
	git apply $binary --cached <"$dotest/patch"
    then
	mv "$dotest/patch-merge-base+" "$dotest/patch-merge-base"
	mv "$dotest/patch-merge-tmp-index" "$dotest/patch-merge-index"
    else
        cannot_fallback "Did you hand edit your patch?
It does not apply to blobs recorded in its index."
    fi

    test -f "$dotest/patch-merge-index" &&
    his_tree=$(GIT_INDEX_FILE="$dotest/patch-merge-index" git write-tree) &&
    orig_tree=$(cat "$dotest/patch-merge-base") &&
    rm -fr "$dotest"/patch-merge-* || exit 1

    echo Falling back to patching base and 3-way merge...

    # This is not so wrong.  Depending on which base we picked,
    # orig_tree may be wildly different from ours, but his_tree
    # has the same set of wildly different changes in parts the
    # patch did not touch, so recursive ends up canceling them,
    # saying that we reverted all those changes.

    eval GITHEAD_$his_tree='"$SUBJECT"'
    export GITHEAD_$his_tree
    git-merge-recursive $orig_tree -- HEAD $his_tree || {
	    git rerere
	    echo Failed to merge in the changes.
	    exit 1
    }
    unset GITHEAD_$his_tree
}

prec=4
dotest=.dotest sign= utf8=t keep= skip= interactive= resolved= binary=
resolvemsg= resume=
git_apply_opt=

while test $# != 0
do
	case "$1" in
	-i|--interactive)
		interactive=t ;;
	-b|--binary)
		binary=t ;;
	-3|--3way)
		threeway=t ;;
	-s--signoff)
		sign=t ;;
	-u|--utf8)
		utf8=t ;; # this is now default
	--no-utf8)
		utf8= ;;
	-k|--keep)
		keep=t ;;
	-r|--resolved)
		resolved=t ;;
	--skip)
		skip=t ;;
	-d|--dotest)
		shift; dotest=$1;;
	--resolvemsg)
		shift; resolvemsg=$1 ;;
	--whitespace)
		git_apply_opt="$git_apply_opt $1=$2"; shift ;;
	-C|-p)
		git_apply_opt="$git_apply_opt $1$2"; shift ;;
	--)
		shift; break ;;
	*)
		usage ;;
	esac
	shift
done

# If the dotest directory exists, but we have finished applying all the
# patches in them, clear it out.
if test -d "$dotest" &&
   last=$(cat "$dotest/last") &&
   next=$(cat "$dotest/next") &&
   test $# != 0 &&
   test "$next" -gt "$last"
then
   rm -fr "$dotest"
fi

if test -d "$dotest"
then
	case "$#,$skip$resolved" in
	0,*t*)
		# Explicit resume command and we do not have file, so
		# we are happy.
		: ;;
	0,)
		# No file input but without resume parameters; catch
		# user error to feed us a patch from standard input
		# when there is already .dotest.  This is somewhat
		# unreliable -- stdin could be /dev/null for example
		# and the caller did not intend to feed us a patch but
		# wanted to continue unattended.
		tty -s
		;;
	*)
		false
		;;
	esac ||
	die "previous dotest directory $dotest still exists but mbox given."
	resume=yes
else
	# Make sure we are not given --skip nor --resolved
	test ",$skip,$resolved," = ,,, ||
		die "Resolve operation not in progress, we are not resuming."

	# Start afresh.
	mkdir -p "$dotest" || exit

	git mailsplit -d"$prec" -o"$dotest" -b -- "$@" > "$dotest/last" ||  {
		rm -fr "$dotest"
		exit 1
	}

	# -b, -s, -u, -k and --whitespace flags are kept for the
	# resuming session after a patch failure.
	# -3 and -i can and must be given when resuming.
	echo "$binary" >"$dotest/binary"
	echo " $ws" >"$dotest/whitespace"
	echo "$sign" >"$dotest/sign"
	echo "$utf8" >"$dotest/utf8"
	echo "$keep" >"$dotest/keep"
	echo 1 >"$dotest/next"
fi

case "$resolved" in
'')
	files=$(git diff-index --cached --name-only HEAD) || exit
	if [ "$files" ]; then
	   echo "Dirty index: cannot apply patches (dirty: $files)" >&2
	   exit 1
	fi
esac

if test "$(cat "$dotest/binary")" = t
then
	binary=--allow-binary-replacement
fi
if test "$(cat "$dotest/utf8")" = t
then
	utf8=-u
else
	utf8=-n
fi
if test "$(cat "$dotest/keep")" = t
then
	keep=-k
fi
ws=`cat "$dotest/whitespace"`
if test "$(cat "$dotest/sign")" = t
then
	SIGNOFF=`git-var GIT_COMMITTER_IDENT | sed -e '
			s/>.*/>/
			s/^/Signed-off-by: /'
		`
else
	SIGNOFF=
fi

last=`cat "$dotest/last"`
this=`cat "$dotest/next"`
if test "$skip" = t
then
	git rerere clear
	this=`expr "$this" + 1`
	resume=
fi

if test "$this" -gt "$last"
then
	echo Nothing to do.
	rm -fr "$dotest"
	exit
fi

while test "$this" -le "$last"
do
	msgnum=`printf "%0${prec}d" $this`
	next=`expr "$this" + 1`
	test -f "$dotest/$msgnum" || {
		resume=
		go_next
		continue
	}

	# If we are not resuming, parse and extract the patch information
	# into separate files:
	#  - info records the authorship and title
	#  - msg is the rest of commit log message
	#  - patch is the patch body.
	#
	# When we are resuming, these files are either already prepared
	# by the user, or the user can tell us to do so by --resolved flag.
	case "$resume" in
	'')
		git mailinfo $keep $utf8 "$dotest/msg" "$dotest/patch" \
			<"$dotest/$msgnum" >"$dotest/info" ||
			stop_here $this

		# skip pine's internal folder data
		grep '^Author: Mail System Internal Data$' \
			<"$dotest"/info >/dev/null &&
			go_next && continue

		test -s $dotest/patch || {
			echo "Patch is empty.  Was it split wrong?"
			stop_here $this
		}
		git stripspace < "$dotest/msg" > "$dotest/msg-clean"
		;;
	esac

	GIT_AUTHOR_NAME="$(sed -n '/^Author/ s/Author: //p' "$dotest/info")"
	GIT_AUTHOR_EMAIL="$(sed -n '/^Email/ s/Email: //p' "$dotest/info")"
	GIT_AUTHOR_DATE="$(sed -n '/^Date/ s/Date: //p' "$dotest/info")"

	if test -z "$GIT_AUTHOR_EMAIL"
	then
		echo "Patch does not have a valid e-mail address."
		stop_here $this
	fi

	export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE

	SUBJECT="$(sed -n '/^Subject/ s/Subject: //p' "$dotest/info")"
	case "$keep_subject" in -k)  SUBJECT="[PATCH] $SUBJECT" ;; esac

	case "$resume" in
	'')
	    if test '' != "$SIGNOFF"
	    then
		LAST_SIGNED_OFF_BY=`
		    sed -ne '/^Signed-off-by: /p' \
		    "$dotest/msg-clean" |
		    tail -n 1
		`
		ADD_SIGNOFF=`
		    test "$LAST_SIGNED_OFF_BY" = "$SIGNOFF" || {
		    test '' = "$LAST_SIGNED_OFF_BY" && echo
		    echo "$SIGNOFF"
		}`
	    else
		ADD_SIGNOFF=
	    fi
	    {
		printf '%s\n' "$SUBJECT"
		if test -s "$dotest/msg-clean"
		then
			echo
			cat "$dotest/msg-clean"
		fi
		if test '' != "$ADD_SIGNOFF"
		then
			echo "$ADD_SIGNOFF"
		fi
	    } >"$dotest/final-commit"
	    ;;
	*)
		case "$resolved$interactive" in
		tt)
			# This is used only for interactive view option.
			git diff-index -p --cached HEAD >"$dotest/patch"
			;;
		esac
	esac

	resume=
	if test "$interactive" = t
	then
	    test -t 0 ||
	    die "cannot be interactive without stdin connected to a terminal."
	    action=again
	    while test "$action" = again
	    do
		echo "Commit Body is:"
		echo "--------------------------"
		cat "$dotest/final-commit"
		echo "--------------------------"
		printf "Apply? [y]es/[n]o/[e]dit/[v]iew patch/[a]ccept all "
		read reply
		case "$reply" in
		[yY]*) action=yes ;;
		[aA]*) action=yes interactive= ;;
		[nN]*) action=skip ;;
		[eE]*) git_editor "$dotest/final-commit"
		       action=again ;;
		[vV]*) action=again
		       LESS=-S ${PAGER:-less} "$dotest/patch" ;;
		*)     action=again ;;
		esac
	    done
	else
	    action=yes
	fi

	if test $action = skip
	then
		go_next
		continue
	fi

	if test -x "$GIT_DIR"/hooks/applypatch-msg
	then
		"$GIT_DIR"/hooks/applypatch-msg "$dotest/final-commit" ||
		stop_here $this
	fi

	printf 'Applying %s\n' "$SUBJECT"

	case "$resolved" in
	'')
		git apply $git_apply_opt $binary --index "$dotest/patch"
		apply_status=$?
		;;
	t)
		# Resolved means the user did all the hard work, and
		# we do not have to do any patch application.  Just
		# trust what the user has in the index file and the
		# working tree.
		resolved=
		git diff-index --quiet --cached HEAD && {
			echo "No changes - did you forget to use 'git add'?"
			stop_here_user_resolve $this
		}
		unmerged=$(git ls-files -u)
		if test -n "$unmerged"
		then
			echo "You still have unmerged paths in your index"
			echo "did you forget to use 'git add'?"
			stop_here_user_resolve $this
		fi
		apply_status=0
		git rerere
		;;
	esac

	if test $apply_status = 1 && test "$threeway" = t
	then
		if (fall_back_3way)
		then
		    # Applying the patch to an earlier tree and merging the
		    # result may have produced the same tree as ours.
		    git diff-index --quiet --cached HEAD && {
			echo No changes -- Patch already applied.
			go_next
			continue
		    }
		    # clear apply_status -- we have successfully merged.
		    apply_status=0
		fi
	fi
	if test $apply_status != 0
	then
		echo Patch failed at $msgnum.
		stop_here_user_resolve $this
	fi

	if test -x "$GIT_DIR"/hooks/pre-applypatch
	then
		"$GIT_DIR"/hooks/pre-applypatch || stop_here $this
	fi

	tree=$(git write-tree) &&
	parent=$(git rev-parse --verify HEAD) &&
	commit=$(git commit-tree $tree -p $parent <"$dotest/final-commit") &&
	git update-ref -m "$GIT_REFLOG_ACTION: $SUBJECT" HEAD $commit $parent ||
	stop_here $this

	if test -x "$GIT_DIR"/hooks/post-applypatch
	then
		"$GIT_DIR"/hooks/post-applypatch
	fi

	git gc --auto

	go_next
done

rm -fr "$dotest"
