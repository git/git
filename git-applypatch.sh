#!/bin/sh
##
## applypatch takes four file arguments, and uses those to
## apply the unpacked patch (surprise surprise) that they
## represent to the current tree.
##
## The arguments are:
##	$1 - file with commit message
##	$2 - file with the actual patch
##	$3 - "info" file with Author, email and subject
##	$4 - optional file containing signoff to add
##

USAGE='<msg> <patch> <info> [<signoff>]'
. git-sh-setup

case "$#" in 3|4) ;; *) usage ;; esac

final=.dotest/final-commit
##
## If this file exists, we ask before applying
##
query_apply=.dotest/.query_apply

## We do not munge the first line of the commit message too much
## if this file exists.
keep_subject=.dotest/.keep_subject

## We do not attempt the 3-way merge fallback unless this file exists.
fall_back_3way=.dotest/.3way

MSGFILE=$1
PATCHFILE=$2
INFO=$3
SIGNOFF=$4
EDIT=${VISUAL:-${EDITOR:-vi}}

export GIT_AUTHOR_NAME="$(sed -n '/^Author/ s/Author: //p' "$INFO")"
export GIT_AUTHOR_EMAIL="$(sed -n '/^Email/ s/Email: //p' "$INFO")"
export GIT_AUTHOR_DATE="$(sed -n '/^Date/ s/Date: //p' "$INFO")"
export SUBJECT="$(sed -n '/^Subject/ s/Subject: //p' "$INFO")"

if test '' != "$SIGNOFF"
then
	if test -f "$SIGNOFF"
	then
		SIGNOFF=`cat "$SIGNOFF"` || exit
	elif case "$SIGNOFF" in yes | true | me | please) : ;; *) false ;; esac
	then
		SIGNOFF=`git-var GIT_COMMITTER_IDENT | sed -e '
				s/>.*/>/
				s/^/Signed-off-by: /'
		`
	else
		SIGNOFF=
	fi
	if test '' != "$SIGNOFF"
	then
		LAST_SIGNED_OFF_BY=`
			sed -ne '/^Signed-off-by: /p' "$MSGFILE" |
			tail -n 1
		`
		test "$LAST_SIGNED_OFF_BY" = "$SIGNOFF" || {
		    test '' = "$LAST_SIGNED_OFF_BY" && echo
		    echo "$SIGNOFF"
		} >>"$MSGFILE"
	fi
fi

patch_header=
test -f "$keep_subject" || patch_header='[PATCH] '

{
	echo "$patch_header$SUBJECT"
	if test -s "$MSGFILE"
	then
		echo
		cat "$MSGFILE"
	fi
} >"$final"

interactive=yes
test -f "$query_apply" || interactive=no

while [ "$interactive" = yes ]; do
	echo "Commit Body is:"
	echo "--------------------------"
	cat "$final"
	echo "--------------------------"
	printf "Apply? [y]es/[n]o/[e]dit/[a]ccept all "
	read reply
	case "$reply" in
		y|Y) interactive=no;;
		n|N) exit 2;;	# special value to tell dotest to keep going
		e|E) "$EDIT" "$final";;
		a|A) rm -f "$query_apply"
		     interactive=no ;;
	esac
done

if test -x "$GIT_DIR"/hooks/applypatch-msg
then
	"$GIT_DIR"/hooks/applypatch-msg "$final" || exit
fi

echo
echo Applying "'$SUBJECT'"
echo

git-apply --index "$PATCHFILE" || {

	# git-apply exits with status 1 when the patch does not apply,
	# but it die()s with other failures, most notably upon corrupt
	# patch.  In the latter case, there is no point to try applying
	# it to another tree and do 3-way merge.
	test $? = 1 || exit 1

	test -f "$fall_back_3way" || exit 1

	# Here if we know which revision the patch applies to,
	# we create a temporary working tree and index, apply the
	# patch, and attempt 3-way merge with the resulting tree.

	O_OBJECT=`cd "$GIT_OBJECT_DIRECTORY" && pwd`
	rm -fr .patch-merge-*

	if git-apply -z --index-info "$PATCHFILE" \
		>.patch-merge-index-info 2>/dev/null &&
		GIT_INDEX_FILE=.patch-merge-tmp-index \
		git-update-index -z --index-info <.patch-merge-index-info &&
		GIT_INDEX_FILE=.patch-merge-tmp-index \
		git-write-tree >.patch-merge-tmp-base &&
		(
			mkdir .patch-merge-tmp-dir &&
			cd .patch-merge-tmp-dir &&
			GIT_INDEX_FILE="../.patch-merge-tmp-index" \
			GIT_OBJECT_DIRECTORY="$O_OBJECT" \
			git-apply $binary --index
		) <"$PATCHFILE"
	then
		echo Using index info to reconstruct a base tree...
		mv .patch-merge-tmp-base .patch-merge-base
		mv .patch-merge-tmp-index .patch-merge-index
	else
	(
		N=10

		# Otherwise, try nearby trees that can be used to apply the
		# patch.
		git-rev-list --max-count=$N HEAD

		# or hoping the patch is against known tags...
		git-ls-remote --tags .
	) |
	    while read base junk
	    do
		# Try it if we have it as a tree.
		git-cat-file tree "$base" >/dev/null 2>&1 || continue

		rm -fr .patch-merge-tmp-* &&
		mkdir .patch-merge-tmp-dir || break
		(
			cd .patch-merge-tmp-dir &&
			GIT_INDEX_FILE=../.patch-merge-tmp-index &&
			GIT_OBJECT_DIRECTORY="$O_OBJECT" &&
			export GIT_INDEX_FILE GIT_OBJECT_DIRECTORY &&
			git-read-tree "$base" &&
			git-apply --index &&
			mv ../.patch-merge-tmp-index ../.patch-merge-index &&
			echo "$base" >../.patch-merge-base
		) <"$PATCHFILE"  2>/dev/null && break
	    done
	fi

	test -f .patch-merge-index &&
	his_tree=$(GIT_INDEX_FILE=.patch-merge-index git-write-tree) &&
	orig_tree=$(cat .patch-merge-base) &&
	rm -fr .patch-merge-* || exit 1

	echo Falling back to patching base and 3-way merge using $orig_tree...

	# This is not so wrong.  Depending on which base we picked,
	# orig_tree may be wildly different from ours, but his_tree
	# has the same set of wildly different changes in parts the
	# patch did not touch, so resolve ends up cancelling them,
	# saying that we reverted all those changes.

	if git-merge-resolve $orig_tree -- HEAD $his_tree
	then
		echo Done.
	else
		echo Failed to merge in the changes.
		exit 1
	fi
}

if test -x "$GIT_DIR"/hooks/pre-applypatch
then
	"$GIT_DIR"/hooks/pre-applypatch || exit
fi

tree=$(git-write-tree) || exit 1
echo Wrote tree $tree
parent=$(git-rev-parse --verify HEAD) &&
commit=$(git-commit-tree $tree -p $parent <"$final") || exit 1
echo Committed: $commit
git-update-ref -m "applypatch: $SUBJECT" HEAD $commit $parent || exit

if test -x "$GIT_DIR"/hooks/post-applypatch
then
	"$GIT_DIR"/hooks/post-applypatch
fi
