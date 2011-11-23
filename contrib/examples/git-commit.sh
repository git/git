#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
# Copyright (c) 2006 Junio C Hamano

USAGE='[-a | --interactive] [-s] [-v] [--no-verify] [-m <message> | -F <logfile> | (-C|-c) <commit> | --amend] [-u] [-e] [--author <author>] [--template <file>] [[-i | -o] <path>...]'
SUBDIRECTORY_OK=Yes
OPTIONS_SPEC=
. git-sh-setup
require_work_tree

git rev-parse --verify HEAD >/dev/null 2>&1 || initial_commit=t

case "$0" in
*status)
	status_only=t
	;;
*commit)
	status_only=
	;;
esac

refuse_partial () {
	echo >&2 "$1"
	echo >&2 "You might have meant to say 'git commit -i paths...', perhaps?"
	exit 1
}

TMP_INDEX=
THIS_INDEX="${GIT_INDEX_FILE:-$GIT_DIR/index}"
NEXT_INDEX="$GIT_DIR/next-index$$"
rm -f "$NEXT_INDEX"
save_index () {
	cp -p "$THIS_INDEX" "$NEXT_INDEX"
}

run_status () {
	# If TMP_INDEX is defined, that means we are doing
	# "--only" partial commit, and that index file is used
	# to build the tree for the commit.  Otherwise, if
	# NEXT_INDEX exists, that is the index file used to
	# make the commit.  Otherwise we are using as-is commit
	# so the regular index file is what we use to compare.
	if test '' != "$TMP_INDEX"
	then
		GIT_INDEX_FILE="$TMP_INDEX"
		export GIT_INDEX_FILE
	elif test -f "$NEXT_INDEX"
	then
		GIT_INDEX_FILE="$NEXT_INDEX"
		export GIT_INDEX_FILE
	fi

	if test "$status_only" = "t" -o "$use_status_color" = "t"; then
		color=
	else
		color=--nocolor
	fi
	git runstatus ${color} \
		${verbose:+--verbose} \
		${amend:+--amend} \
		${untracked_files:+--untracked}
}

trap '
	test -z "$TMP_INDEX" || {
		test -f "$TMP_INDEX" && rm -f "$TMP_INDEX"
	}
	rm -f "$NEXT_INDEX"
' 0

################################################################
# Command line argument parsing and sanity checking

all=
also=
allow_empty=f
interactive=
only=
logfile=
use_commit=
amend=
edit_flag=
no_edit=
log_given=
log_message=
verify=t
quiet=
verbose=
signoff=
force_author=
only_include_assumed=
untracked_files=
templatefile="`git config commit.template`"
while test $# != 0
do
	case "$1" in
	-F|--F|-f|--f|--fi|--fil|--file)
		case "$#" in 1) usage ;; esac
		shift
		no_edit=t
		log_given=t$log_given
		logfile="$1"
		;;
	-F*|-f*)
		no_edit=t
		log_given=t$log_given
		logfile="${1#-[Ff]}"
		;;
	--F=*|--f=*|--fi=*|--fil=*|--file=*)
		no_edit=t
		log_given=t$log_given
		logfile="${1#*=}"
		;;
	-a|--a|--al|--all)
		all=t
		;;
	--allo|--allow|--allow-|--allow-e|--allow-em|--allow-emp|\
	--allow-empt|--allow-empty)
		allow_empty=t
		;;
	--au=*|--aut=*|--auth=*|--autho=*|--author=*)
		force_author="${1#*=}"
		;;
	--au|--aut|--auth|--autho|--author)
		case "$#" in 1) usage ;; esac
		shift
		force_author="$1"
		;;
	-e|--e|--ed|--edi|--edit)
		edit_flag=t
		;;
	-i|--i|--in|--inc|--incl|--inclu|--includ|--include)
		also=t
		;;
	--int|--inte|--inter|--intera|--interac|--interact|--interacti|\
	--interactiv|--interactive)
		interactive=t
		;;
	-o|--o|--on|--onl|--only)
		only=t
		;;
	-m|--m|--me|--mes|--mess|--messa|--messag|--message)
		case "$#" in 1) usage ;; esac
		shift
		log_given=m$log_given
		log_message="${log_message:+${log_message}

}$1"
		no_edit=t
		;;
	-m*)
		log_given=m$log_given
		log_message="${log_message:+${log_message}

}${1#-m}"
		no_edit=t
		;;
	--m=*|--me=*|--mes=*|--mess=*|--messa=*|--messag=*|--message=*)
		log_given=m$log_given
		log_message="${log_message:+${log_message}

}${1#*=}"
		no_edit=t
		;;
	-n|--n|--no|--no-|--no-v|--no-ve|--no-ver|--no-veri|--no-verif|\
	--no-verify)
		verify=
		;;
	--a|--am|--ame|--amen|--amend)
		amend=t
		use_commit=HEAD
		;;
	-c)
		case "$#" in 1) usage ;; esac
		shift
		log_given=t$log_given
		use_commit="$1"
		no_edit=
		;;
	--ree=*|--reed=*|--reedi=*|--reedit=*|--reedit-=*|--reedit-m=*|\
	--reedit-me=*|--reedit-mes=*|--reedit-mess=*|--reedit-messa=*|\
	--reedit-messag=*|--reedit-message=*)
		log_given=t$log_given
		use_commit="${1#*=}"
		no_edit=
		;;
	--ree|--reed|--reedi|--reedit|--reedit-|--reedit-m|--reedit-me|\
	--reedit-mes|--reedit-mess|--reedit-messa|--reedit-messag|\
	--reedit-message)
		case "$#" in 1) usage ;; esac
		shift
		log_given=t$log_given
		use_commit="$1"
		no_edit=
		;;
	-C)
		case "$#" in 1) usage ;; esac
		shift
		log_given=t$log_given
		use_commit="$1"
		no_edit=t
		;;
	--reu=*|--reus=*|--reuse=*|--reuse-=*|--reuse-m=*|--reuse-me=*|\
	--reuse-mes=*|--reuse-mess=*|--reuse-messa=*|--reuse-messag=*|\
	--reuse-message=*)
		log_given=t$log_given
		use_commit="${1#*=}"
		no_edit=t
		;;
	--reu|--reus|--reuse|--reuse-|--reuse-m|--reuse-me|--reuse-mes|\
	--reuse-mess|--reuse-messa|--reuse-messag|--reuse-message)
		case "$#" in 1) usage ;; esac
		shift
		log_given=t$log_given
		use_commit="$1"
		no_edit=t
		;;
	-s|--s|--si|--sig|--sign|--signo|--signof|--signoff)
		signoff=t
		;;
	-t|--t|--te|--tem|--temp|--templ|--templa|--templat|--template)
		case "$#" in 1) usage ;; esac
		shift
		templatefile="$1"
		no_edit=
		;;
	-q|--q|--qu|--qui|--quie|--quiet)
		quiet=t
		;;
	-v|--v|--ve|--ver|--verb|--verbo|--verbos|--verbose)
		verbose=t
		;;
	-u|--u|--un|--unt|--untr|--untra|--untrac|--untrack|--untracke|\
	--untracked|--untracked-|--untracked-f|--untracked-fi|--untracked-fil|\
	--untracked-file|--untracked-files)
		untracked_files=t
		;;
	--)
		shift
		break
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
case "$edit_flag" in t) no_edit= ;; esac

################################################################
# Sanity check options

case "$amend,$initial_commit" in
t,t)
	die "You do not have anything to amend." ;;
t,)
	if [ -f "$GIT_DIR/MERGE_HEAD" ]; then
		die "You are in the middle of a merge -- cannot amend."
	fi ;;
esac

case "$log_given" in
tt*)
	die "Only one of -c/-C/-F can be used." ;;
*tm*|*mt*)
	die "Option -m cannot be combined with -c/-C/-F." ;;
esac

case "$#,$also,$only,$amend" in
*,t,t,*)
	die "Only one of --include/--only can be used." ;;
0,t,,* | 0,,t,)
	die "No paths with --include/--only does not make sense." ;;
0,,t,t)
	only_include_assumed="# Clever... amending the last one with dirty index." ;;
0,,,*)
	;;
*,,,*)
	only_include_assumed="# Explicit paths specified without -i nor -o; assuming --only paths..."
	also=
	;;
esac
unset only
case "$all,$interactive,$also,$#" in
*t,*t,*)
	die "Cannot use -a, --interactive or -i at the same time." ;;
t,,,[1-9]*)
	die "Paths with -a does not make sense." ;;
,t,,[1-9]*)
	die "Paths with --interactive does not make sense." ;;
,,t,0)
	die "No paths with -i does not make sense." ;;
esac

if test ! -z "$templatefile" -a -z "$log_given"
then
	if test ! -f "$templatefile"
	then
		die "Commit template file does not exist."
	fi
fi

################################################################
# Prepare index to have a tree to be committed

case "$all,$also" in
t,)
	if test ! -f "$THIS_INDEX"
	then
		die 'nothing to commit (use "git add file1 file2" to include for commit)'
	fi
	save_index &&
	(
		cd_to_toplevel &&
		GIT_INDEX_FILE="$NEXT_INDEX" &&
		export GIT_INDEX_FILE &&
		git diff-files --name-only -z |
		git update-index --remove -z --stdin
	) || exit
	;;
,t)
	save_index &&
	git ls-files --error-unmatch -- "$@" >/dev/null || exit

	git diff-files --name-only -z -- "$@"  |
	(
		cd_to_toplevel &&
		GIT_INDEX_FILE="$NEXT_INDEX" &&
		export GIT_INDEX_FILE &&
		git update-index --remove -z --stdin
	) || exit
	;;
,)
	if test "$interactive" = t; then
		git add --interactive || exit
	fi
	case "$#" in
	0)
		;; # commit as-is
	*)
		if test -f "$GIT_DIR/MERGE_HEAD"
		then
			refuse_partial "Cannot do a partial commit during a merge."
		fi

		TMP_INDEX="$GIT_DIR/tmp-index$$"
		W=
		test -z "$initial_commit" && W=--with-tree=HEAD
		commit_only=`git ls-files --error-unmatch $W -- "$@"` || exit

		# Build a temporary index and update the real index
		# the same way.
		if test -z "$initial_commit"
		then
			GIT_INDEX_FILE="$THIS_INDEX" \
			git read-tree --index-output="$TMP_INDEX" -i -m HEAD
		else
			rm -f "$TMP_INDEX"
		fi || exit

		printf '%s\n' "$commit_only" |
		GIT_INDEX_FILE="$TMP_INDEX" \
		git update-index --add --remove --stdin &&

		save_index &&
		printf '%s\n' "$commit_only" |
		(
			GIT_INDEX_FILE="$NEXT_INDEX"
			export GIT_INDEX_FILE
			git update-index --add --remove --stdin
		) || exit
		;;
	esac
	;;
esac

################################################################
# If we do as-is commit, the index file will be THIS_INDEX,
# otherwise NEXT_INDEX after we make this commit.  We leave
# the index as is if we abort.

if test -f "$NEXT_INDEX"
then
	USE_INDEX="$NEXT_INDEX"
else
	USE_INDEX="$THIS_INDEX"
fi

case "$status_only" in
t)
	# This will silently fail in a read-only repository, which is
	# what we want.
	GIT_INDEX_FILE="$USE_INDEX" git update-index -q --unmerged --refresh
	run_status
	exit $?
	;;
'')
	GIT_INDEX_FILE="$USE_INDEX" git update-index -q --refresh || exit
	;;
esac

################################################################
# Grab commit message, write out tree and make commit.

if test t = "$verify" && test -x "$GIT_DIR"/hooks/pre-commit
then
    GIT_INDEX_FILE="${TMP_INDEX:-${USE_INDEX}}" "$GIT_DIR"/hooks/pre-commit \
    || exit
fi

if test "$log_message" != ''
then
	printf '%s\n' "$log_message"
elif test "$logfile" != ""
then
	if test "$logfile" = -
	then
		test -t 0 &&
		echo >&2 "(reading log message from standard input)"
		cat
	else
		cat <"$logfile"
	fi
elif test "$use_commit" != ""
then
	encoding=$(git config i18n.commitencoding || echo UTF-8)
	git show -s --pretty=raw --encoding="$encoding" "$use_commit" |
	sed -e '1,/^$/d' -e 's/^    //'
elif test -f "$GIT_DIR/MERGE_MSG"
then
	cat "$GIT_DIR/MERGE_MSG"
elif test -f "$GIT_DIR/SQUASH_MSG"
then
	cat "$GIT_DIR/SQUASH_MSG"
elif test "$templatefile" != ""
then
	cat "$templatefile"
fi | git stripspace >"$GIT_DIR"/COMMIT_EDITMSG

case "$signoff" in
t)
	sign=$(git var GIT_COMMITTER_IDENT | sed -e '
		s/>.*/>/
		s/^/Signed-off-by: /
		')
	blank_before_signoff=
	tail -n 1 "$GIT_DIR"/COMMIT_EDITMSG |
	grep 'Signed-off-by:' >/dev/null || blank_before_signoff='
'
	tail -n 1 "$GIT_DIR"/COMMIT_EDITMSG |
	grep "$sign"$ >/dev/null ||
	printf '%s%s\n' "$blank_before_signoff" "$sign" \
		>>"$GIT_DIR"/COMMIT_EDITMSG
	;;
esac

if test -f "$GIT_DIR/MERGE_HEAD" && test -z "$no_edit"; then
	echo "#"
	echo "# It looks like you may be committing a MERGE."
	echo "# If this is not correct, please remove the file"
	printf '%s\n' "#	$GIT_DIR/MERGE_HEAD"
	echo "# and try again"
	echo "#"
fi >>"$GIT_DIR"/COMMIT_EDITMSG

# Author
if test '' != "$use_commit"
then
	eval "$(get_author_ident_from_commit "$use_commit")"
	export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE
fi
if test '' != "$force_author"
then
	GIT_AUTHOR_NAME=`expr "z$force_author" : 'z\(.*[^ ]\) *<.*'` &&
	GIT_AUTHOR_EMAIL=`expr "z$force_author" : '.*\(<.*\)'` &&
	test '' != "$GIT_AUTHOR_NAME" &&
	test '' != "$GIT_AUTHOR_EMAIL" ||
	die "malformed --author parameter"
	export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL
fi

PARENTS="-p HEAD"
if test -z "$initial_commit"
then
	rloga='commit'
	if [ -f "$GIT_DIR/MERGE_HEAD" ]; then
		rloga='commit (merge)'
		PARENTS="-p HEAD "`sed -e 's/^/-p /' "$GIT_DIR/MERGE_HEAD"`
	elif test -n "$amend"; then
		rloga='commit (amend)'
		PARENTS=$(git cat-file commit HEAD |
			sed -n -e '/^$/q' -e 's/^parent /-p /p')
	fi
	current="$(git rev-parse --verify HEAD)"
else
	if [ -z "$(git ls-files)" ]; then
		echo >&2 'nothing to commit (use "git add file1 file2" to include for commit)'
		exit 1
	fi
	PARENTS=""
	rloga='commit (initial)'
	current=''
fi
set_reflog_action "$rloga"

if test -z "$no_edit"
then
	{
		echo ""
		echo "# Please enter the commit message for your changes."
		echo "# (Comment lines starting with '#' will not be included)"
		test -z "$only_include_assumed" || echo "$only_include_assumed"
		run_status
	} >>"$GIT_DIR"/COMMIT_EDITMSG
else
	# we need to check if there is anything to commit
	run_status >/dev/null
fi
case "$allow_empty,$?,$PARENTS" in
t,* | ?,0,* | ?,*,-p' '?*-p' '?*)
	# an explicit --allow-empty, or a merge commit can record the
	# same tree as its parent.  Otherwise having commitable paths
	# is required.
	;;
*)
	rm -f "$GIT_DIR/COMMIT_EDITMSG" "$GIT_DIR/SQUASH_MSG"
	use_status_color=t
	run_status
	exit 1
esac

case "$no_edit" in
'')
	git var GIT_AUTHOR_IDENT > /dev/null  || die
	git var GIT_COMMITTER_IDENT > /dev/null  || die
	git_editor "$GIT_DIR/COMMIT_EDITMSG"
	;;
esac

case "$verify" in
t)
	if test -x "$GIT_DIR"/hooks/commit-msg
	then
		"$GIT_DIR"/hooks/commit-msg "$GIT_DIR"/COMMIT_EDITMSG || exit
	fi
esac

if test -z "$no_edit"
then
    sed -e '
        /^diff --git a\/.*/{
	    s///
	    q
	}
	/^#/d
    ' "$GIT_DIR"/COMMIT_EDITMSG
else
    cat "$GIT_DIR"/COMMIT_EDITMSG
fi |
git stripspace >"$GIT_DIR"/COMMIT_MSG

# Test whether the commit message has any content we didn't supply.
have_commitmsg=
grep -v -i '^Signed-off-by' "$GIT_DIR"/COMMIT_MSG |
	git stripspace > "$GIT_DIR"/COMMIT_BAREMSG

# Is the commit message totally empty?
if test -s "$GIT_DIR"/COMMIT_BAREMSG
then
	if test "$templatefile" != ""
	then
		# Test whether this is just the unaltered template.
		if cnt=`sed -e '/^#/d' < "$templatefile" |
			git stripspace |
			diff "$GIT_DIR"/COMMIT_BAREMSG - |
			wc -l` &&
		   test 0 -lt $cnt
		then
			have_commitmsg=t
		fi
	else
		# No template, so the content in the commit message must
		# have come from the user.
		have_commitmsg=t
	fi
fi

rm -f "$GIT_DIR"/COMMIT_BAREMSG

if test "$have_commitmsg" = "t"
then
	if test -z "$TMP_INDEX"
	then
		tree=$(GIT_INDEX_FILE="$USE_INDEX" git write-tree)
	else
		tree=$(GIT_INDEX_FILE="$TMP_INDEX" git write-tree) &&
		rm -f "$TMP_INDEX"
	fi &&
	commit=$(git commit-tree $tree $PARENTS <"$GIT_DIR/COMMIT_MSG") &&
	rlogm=$(sed -e 1q "$GIT_DIR"/COMMIT_MSG) &&
	git update-ref -m "$GIT_REFLOG_ACTION: $rlogm" HEAD $commit "$current" &&
	rm -f -- "$GIT_DIR/MERGE_HEAD" "$GIT_DIR/MERGE_MSG" &&
	if test -f "$NEXT_INDEX"
	then
		mv "$NEXT_INDEX" "$THIS_INDEX"
	else
		: ;# happy
	fi
else
	echo >&2 "* no commit message?  aborting commit."
	false
fi
ret="$?"
rm -f "$GIT_DIR/COMMIT_MSG" "$GIT_DIR/COMMIT_EDITMSG" "$GIT_DIR/SQUASH_MSG"

cd_to_toplevel

git rerere

if test "$ret" = 0
then
	git gc --auto
	if test -x "$GIT_DIR"/hooks/post-commit
	then
		"$GIT_DIR"/hooks/post-commit
	fi
	if test -z "$quiet"
	then
		commit=`git diff-tree --always --shortstat --pretty="format:%h: %s"\
		       --abbrev --summary --root HEAD --`
		echo "Created${initial_commit:+ initial} commit $commit"
	fi
fi

exit "$ret"
