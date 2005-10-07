#!/bin/sh
#
#
. git-sh-setup || die "Not a git archive"

files=$(git-diff-index --cached --name-only HEAD) || exit
if [ "$files" ]; then
   echo "Dirty index: cannot apply patches (dirty: $files)" >&2
   exit 1
fi

usage () {
    echo >&2 "usage: $0 [--signoff] [--dotest=<dir>] [--utf8] [--3way] <mbox>"
    echo >&2 "	or, when resuming"
    echo >&2 "	$0 [--skip]"
    exit 1;
}

stop_here () {
    echo "$1" >"$dotest/next"
    exit 1
}

go_next () {
	rm -f "$dotest/$msgnum" "$dotest/msg" "$dotest/msg-clean" \
		"$dotest/patch" "$dotest/info"
	echo "$next" >"$dotest/next"
	this=$next
}

fall_back_3way () {
    O_OBJECT=`cd "$GIT_OBJECT_DIRECTORY" && pwd`

    rm -fr "$dotest"/patch-merge-*
    mkdir "$dotest/patch-merge-tmp-dir"

    # First see if the patch records the index info that we can use.
    if git-apply --show-index-info "$dotest/patch" \
	>"$dotest/patch-merge-index-info" 2>/dev/null &&
	GIT_INDEX_FILE="$dotest/patch-merge-tmp-index" \
	git-update-index --index-info <"$dotest/patch-merge-index-info" &&
	GIT_INDEX_FILE="$dotest/patch-merge-tmp-index" \
	git-write-tree >"$dotest/patch-merge-base+" &&
	# index has the base tree now.
	(
	    cd "$dotest/patch-merge-tmp-dir" &&
	    GIT_INDEX_FILE="../patch-merge-tmp-index" \
	    GIT_OBJECT_DIRECTORY="$O_OBJECT" \
	    git-apply --index <../patch
        )
    then
	echo Using index info to reconstruct a base tree...
	mv "$dotest/patch-merge-base+" "$dotest/patch-merge-base"
	mv "$dotest/patch-merge-tmp-index" "$dotest/patch-merge-index"
    else
	# Otherwise, try nearby trees that can be used to apply the
	# patch.
	(
	    N=10

	    # Hoping the patch is against our recent commits...
	    git-rev-list --max-count=$N HEAD

	    # or hoping the patch is against known tags...
	    git-ls-remote --tags .
	) |
	while read base junk
	do
	    # See if we have it as a tree...
	    git-cat-file tree "$base" >/dev/null 2>&1 || continue

	    rm -fr "$dotest"/patch-merge-* &&
	    mkdir "$dotest/patch-merge-tmp-dir" || break
	    (
		cd "$dotest/patch-merge-tmp-dir" &&
		GIT_INDEX_FILE=../patch-merge-tmp-index &&
		GIT_OBJECT_DIRECTORY="$O_OBJECT" &&
		export GIT_INDEX_FILE GIT_OBJECT_DIRECTORY &&
		git-read-tree "$base" &&
		git-apply --index &&
		mv ../patch-merge-tmp-index ../patch-merge-index &&
		echo "$base" >../patch-merge-base
	    ) <"$dotest/patch"  2>/dev/null && break
	done
    fi

    test -f "$dotest/patch-merge-index" &&
    his_tree=$(GIT_INDEX_FILE="$dotest/patch-merge-index" git-write-tree) &&
    orig_tree=$(cat "$dotest/patch-merge-base") &&
    rm -fr "$dotest"/patch-merge-* || exit 1

    echo Falling back to patching base and 3-way merge...

    # This is not so wrong.  Depending on which base we picked,
    # orig_tree may be wildly different from ours, but his_tree
    # has the same set of wildly different changes in parts the
    # patch did not touch, so resolve ends up cancelling them,
    # saying that we reverted all those changes.

    git-merge-resolve $orig_tree -- HEAD $his_tree || {
	    echo Failed to merge in the changes.
	    exit 1
    }
}

prec=4
dotest=.dotest sign= utf8= keep= skip= interactive=

while case "$#" in 0) break;; esac
do
	case "$1" in
	-d=*|--d=*|--do=*|--dot=*|--dote=*|--dotes=*|--dotest=*)
	dotest=`expr "$1" : '-[^=]*=\(.*\)'`; shift ;;
	-d|--d|--do|--dot|--dote|--dotes|--dotest)
	case "$#" in 1) usage ;; esac; shift
	dotest="$1"; shift;;

	-i|--i|--in|--int|--inte|--inter|--intera|--interac|--interact|\
	--interacti|--interactiv|--interactive)
	interactive=t; shift ;;

	-3|--3|--3w|--3wa|--3way)
	threeway=t; shift ;;
	-s|--s|--si|--sig|--sign|--signo|--signof|--signoff)
	sign=t; shift ;;
	-u|--u|--ut|--utf|--utf8)
	utf8=t; shift ;;
	-k|--k|--ke|--kee|--keep)
	keep=t; shift ;;

	--sk|--ski|--skip)
	skip=t; shift ;;

	--)
	shift; break ;;
	-*)
	usage ;;
	*)
	break ;;
	esac
done

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
	test ",$#," = ",0," ||
	die "previous dotest directory $dotest still exists but mbox given."
else
	# Make sure we are not given --skip
	test ",$skip," = ,, ||
	die "we are not resuming."

	# Start afresh.
	mkdir -p "$dotest" || exit

	# cat does the right thing for us, including '-' to mean
	# standard input.
	cat "$@" |
	git-mailsplit -d$prec "$dotest/" >"$dotest/last" || {
		rm -fr "$dotest"
		exit 1
	}

	echo "$sign" >"$dotest/sign"
	echo "$utf8" >"$dotest/utf8"
	echo "$keep" >"$dotest/keep"
	echo "$threeway" >"$dotest/3way"
	echo 1 >"$dotest/next"
fi

if test "$(cat "$dotest/utf8")" = t
then
	utf8=-u
fi
if test "$(cat "$dotest/keep")" = t
then
	keep=-k
fi
if test "$(cat "$dotest/sign")" = t
then
	SIGNOFF=`git-var GIT_COMMITTER_IDENT | sed -e '
			s/>.*/>/
			s/^/Signed-off-by: /'
		`
else
	SIGNOFF=
fi
threeway=$(cat "$dotest/3way")

last=`cat "$dotest/last"`
this=`cat "$dotest/next"`
if test "$skip" = t
then
	this=`expr "$this" + 1`
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
		go_next
		continue
	}
	git-mailinfo $keep $utf8 "$dotest/msg" "$dotest/patch" \
		<"$dotest/$msgnum" >"$dotest/info" ||
		stop_here $this
	git-stripspace < "$dotest/msg" > "$dotest/msg-clean"

	GIT_AUTHOR_NAME="$(sed -n '/^Author/ s/Author: //p' "$dotest/info")"
	GIT_AUTHOR_EMAIL="$(sed -n '/^Email/ s/Email: //p' "$dotest/info")"
	GIT_AUTHOR_DATE="$(sed -n '/^Date/ s/Date: //p' "$dotest/info")"
	SUBJECT="$(sed -n '/^Subject/ s/Subject: //p' "$dotest/info")"

	case "$keep_subject" in -k)  SUBJECT="[PATCH] $SUBJECT" ;; esac
	if test '' != "$SIGNOFF"
	then
		LAST_SIGNED_OFF_BY=`
			sed -ne '/^Signed-off-by: /p' "$dotest/msg-clean" |
			tail -n 1
		`
		ADD_SIGNOFF=$(test "$LAST_SIGNED_OFF_BY" = "$SIGNOFF" || {
		    test '' = "$LAST_SIGNED_OFF_BY" && echo
		    echo "$SIGNOFF"
		})
	else
		ADD_SIGNOFF=
	fi
	{
		echo "$SUBJECT"
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

	if test "$interactive" = t
	then
	    action=again
	    while test "$action" = again
	    do
		echo "Commit Body is:"
		echo "--------------------------"
		cat "$dotest/final-commit"
		echo "--------------------------"
		echo -n "Apply? [y]es/[n]o/[e]dit/[a]ccept all "
		read reply
		case "$reply" in
		y*|Y*) action=yes ;;
		a*|A*) action=yes interactive= ;;
		n*|N*) action=skip ;;
		e*|E*) "${VISUAL:-${EDITOR:-vi}}" "$dotest/final-commit"
		       action=again ;;
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

	echo
	echo "Applying '$SUBJECT'"
	echo

	git-apply --index "$dotest/patch"; apply_status=$?
	if test $apply_status = 1 && test "$threeway" = t
	then
		(fall_back_3way) || stop_here $this

		# Applying the patch to an earlier tree and merging the
		# result may have produced the same tree as ours.
		if test '' = "$(git-diff-index --cached --name-only -z HEAD)"
		then
			echo No changes -- Patch already applied.
			go_next
			continue
		fi
	fi
	if test $apply_status != 0
	then
		echo Patch failed at $msgnum.
		stop_here $this
	fi

	if test -x "$GIT_DIR"/hooks/pre-applypatch
	then
		"$GIT_DIR"/hooks/pre-applypatch || stop_here $this
	fi

	tree=$(git-write-tree) &&
	echo Wrote tree $tree &&
	parent=$(git-rev-parse --verify HEAD) &&
	commit=$(git-commit-tree $tree -p $parent <"$dotest/final-commit") &&
	echo Committed: $commit &&
	git-update-ref HEAD $commit $parent ||
	stop_here $this

	if test -x "$GIT_DIR"/hooks/post-applypatch
	then
		"$GIT_DIR"/hooks/post-applypatch
	fi

	go_next
done

rm -fr "$dotest"
