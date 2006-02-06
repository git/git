#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
# Copyright (c) 2006 Junio C Hamano

USAGE='[-a] [-i] [-s] [-v | --no-verify]  [-m <message> | -F <logfile> | (-C|-c) <commit>] [-e] [--author <author>] [<path>...]'

SUBDIRECTORY_OK=Yes
. git-sh-setup

git-rev-parse --verify HEAD >/dev/null 2>&1 ||
initial_commit=t

refuse_partial () {
	echo >&2 "$1"
	echo >&2 "You might have meant to say 'git commit -i paths...', perhaps?"
	exit 1
}

SAVE_INDEX="$GIT_DIR/save-index$$"
save_index () {
	cp "$GIT_DIR/index" "$SAVE_INDEX"
}

run_status () {
	(
		cd "$TOP"
		if test '' != "$TMP_INDEX"
		then
			GIT_INDEX_FILE="$TMP_INDEX" git-status
		else
			git-status
		fi
	)
}

all=
also=
only=
logfile=
use_commit=
no_edit=
log_given=
log_message=
verify=t
signoff=
force_author=
while case "$#" in 0) break;; esac
do
  case "$1" in
  -F|--F|-f|--f|--fi|--fil|--file)
      case "$#" in 1) usage ;; esac
      shift
      no_edit=t
      log_given=t$log_given
      logfile="$1"
      shift
      ;;
  -F*|-f*)
      no_edit=t
      log_given=t$log_given
      logfile=`expr "$1" : '-[Ff]\(.*\)'`
      shift
      ;;
  --F=*|--f=*|--fi=*|--fil=*|--file=*)
      no_edit=t
      log_given=t$log_given
      logfile=`expr "$1" : '-[^=]*=\(.*\)'`
      shift
      ;;
  -a|--a|--al|--all)
      all=t
      shift
      ;;
  --au=*|--aut=*|--auth=*|--autho=*|--author=*)
      force_author=`expr "$1" : '-[^=]*=\(.*\)'`
      shift
      ;;
  --au|--aut|--auth|--autho|--author)
      case "$#" in 1) usage ;; esac
      shift
      force_author="$1"
      shift
      ;;
  -e|--e|--ed|--edi|--edit)
      no_edit=
      shift
      ;;
  -i|--i|--in|--inc|--incl|--inclu|--includ|--include)
      also=t
      shift
      ;;
  -o|--o|--on|--onl|--only)
      only=t
      shift
      ;;
  -m|--m|--me|--mes|--mess|--messa|--messag|--message)
      case "$#" in 1) usage ;; esac
      shift
      log_given=t$log_given
      log_message="$1"
      no_edit=t
      shift
      ;;
  -m*)
      log_given=t$log_given
      log_message=`expr "$1" : '-m\(.*\)'`
      no_edit=t
      shift
      ;;
  --m=*|--me=*|--mes=*|--mess=*|--messa=*|--messag=*|--message=*)
      log_given=t$log_given
      log_message=`expr "$1" : '-[^=]*=\(.*\)'`
      no_edit=t
      shift
      ;;
  -n|--n|--no|--no-|--no-v|--no-ve|--no-ver|--no-veri|--no-verif|--no-verify)
      verify=
      shift
      ;;
  -c)
      case "$#" in 1) usage ;; esac
      shift
      log_given=t$log_given
      use_commit="$1"
      no_edit=
      shift
      ;;
  --ree=*|--reed=*|--reedi=*|--reedit=*|--reedit-=*|--reedit-m=*|\
  --reedit-me=*|--reedit-mes=*|--reedit-mess=*|--reedit-messa=*|\
  --reedit-messag=*|--reedit-message=*)
      log_given=t$log_given
      use_commit=`expr "$1" : '-[^=]*=\(.*\)'`
      no_edit=
      shift
      ;;
  --ree|--reed|--reedi|--reedit|--reedit-|--reedit-m|--reedit-me|\
  --reedit-mes|--reedit-mess|--reedit-messa|--reedit-messag|--reedit-message)
      case "$#" in 1) usage ;; esac
      shift
      log_given=t$log_given
      use_commit="$1"
      no_edit=
      shift
      ;;
  -C)
      case "$#" in 1) usage ;; esac
      shift
      log_given=t$log_given
      use_commit="$1"
      no_edit=t
      shift
      ;;
  --reu=*|--reus=*|--reuse=*|--reuse-=*|--reuse-m=*|--reuse-me=*|\
  --reuse-mes=*|--reuse-mess=*|--reuse-messa=*|--reuse-messag=*|\
  --reuse-message=*)
      log_given=t$log_given
      use_commit=`expr "$1" : '-[^=]*=\(.*\)'`
      no_edit=t
      shift
      ;;
  --reu|--reus|--reuse|--reuse-|--reuse-m|--reuse-me|--reuse-mes|\
  --reuse-mess|--reuse-messa|--reuse-messag|--reuse-message)
      case "$#" in 1) usage ;; esac
      shift
      log_given=t$log_given
      use_commit="$1"
      no_edit=t
      shift
      ;;
  -s|--s|--si|--sig|--sign|--signo|--signof|--signoff)
      signoff=t
      shift
      ;;
  -v|--v|--ve|--ver|--veri|--verif|--verify)
      verify=t
     shift
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
done

case "$log_given" in
tt*)
  die "Only one of -c/-C/-F/-m can be used." ;;
esac

case "$#,$also$only" in
*,tt)
  die "Only one of --include/--only can be used." ;;
0,t)
  die "No paths with --include/--only does not make sense." ;;
0,)
  ;;
*,)
  echo >&2 "assuming --include paths..."
  also=t
  # Later when switch the defaults, we will replace them with these:
  # echo >&2 "assuming --only paths..."
  # also=
  ;;
esac
unset only

TOP=`git-rev-parse --show-cdup`
if test -z "$TOP"
then
	TOP=./
fi

case "$all,$also" in
t,t)
	die "Cannot use -a and -i at the same time." ;;
t,)
	case "$#" in
	0) ;;
	*) die "Paths with -a does not make sense." ;;
	esac

	save_index &&
	(
		cd "$TOP"
		git-diff-files --name-only -z |
		git-update-index --remove -z --stdin
	)
	;;
,t)
	case "$#" in
	0) die "No paths with -i does not make sense." ;;
	esac

	save_index &&
	git-diff-files --name-only -z -- "$@"  |
	(cd "$TOP" && git-update-index --remove -z --stdin)
	;;
,)
	case "$#" in
	0)
	    ;; # commit as-is
	*)
	    if test -f "$GIT_DIR/MERGE_HEAD"
	    then
		refuse_partial "Cannot do a partial commit during a merge."
	    fi
	    TMP_INDEX="$GIT_DIR/tmp-index$$"
	    if test -z "$initial_commit"
	    then
		# make sure index is clean at the specified paths, or
		# they are additions.
		dirty_in_index=`git-diff-index --cached --name-status \
			--diff-filter=DMTU HEAD -- "$@"`
		test -z "$dirty_in_index" ||
		refuse_partial "Different in index and the last commit:
$dirty_in_index"
	    fi
	    commit_only=`git-ls-files -- "$@"` ;;
	esac
	;;
esac

git-update-index -q --refresh || exit 1

trap '
	test -z "$TMP_INDEX" || {
		test -f "$TMP_INDEX" && rm -f "$TMP_INDEX"
	}
	test -f "$SAVE_INDEX" && mv -f "$SAVE_INDEX" "$GIT_DIR/index"
' 0

if test "$TMP_INDEX"
then
	if test -z "$initial_commit"
	then
		GIT_INDEX_FILE="$TMP_INDEX" git-read-tree HEAD
	else
		rm -f "$TMP_INDEX"
	fi || exit
	echo "$commit_only" |
	GIT_INDEX_FILE="$TMP_INDEX" git-update-index --add --remove --stdin &&
	save_index &&
	echo "$commit_only" |
	git-update-index --remove --stdin ||
	exit
fi

if test t = "$verify" && test -x "$GIT_DIR"/hooks/pre-commit
then
	if test "$TMP_INDEX"
	then
		GIT_INDEX_FILE="$TMP_INDEX" "$GIT_DIR"/hooks/pre-commit
	else
		"$GIT_DIR"/hooks/pre-commit
	fi || exit
fi

if test "$log_message" != ''
then
	echo "$log_message"
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
	git-cat-file commit "$use_commit" | sed -e '1,/^$/d'
elif test -f "$GIT_DIR/MERGE_HEAD" && test -f "$GIT_DIR/MERGE_MSG"
then
	cat "$GIT_DIR/MERGE_MSG"
fi | git-stripspace >"$GIT_DIR"/COMMIT_EDITMSG

case "$signoff" in
t)
	{
		echo
		git-var GIT_COMMITTER_IDENT | sed -e '
			s/>.*/>/
			s/^/Signed-off-by: /
		'
	} >>"$GIT_DIR"/COMMIT_EDITMSG
	;;
esac

if [ -f "$GIT_DIR/MERGE_HEAD" ]; then
	echo "#"
	echo "# It looks like you may be committing a MERGE."
	echo "# If this is not correct, please remove the file"
	echo "#	$GIT_DIR/MERGE_HEAD"
	echo "# and try again"
	echo "#"
fi >>"$GIT_DIR"/COMMIT_EDITMSG

# Author
if test '' != "$force_author"
then
	GIT_AUTHOR_NAME=`expr "$force_author" : '\(.*[^ ]\) *<.*'` &&
	GIT_AUTHOR_EMAIL=`expr "$force_author" : '.*\(<.*\)'` &&
	test '' != "$GIT_AUTHOR_NAME" &&
	test '' != "$GIT_AUTHOR_EMAIL" ||
	die "malformatted --author parameter"
	export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL
elif test '' != "$use_commit"
then
	pick_author_script='
	/^author /{
		s/'\''/'\''\\'\'\''/g
		h
		s/^author \([^<]*\) <[^>]*> .*$/\1/
		s/'\''/'\''\'\'\''/g
		s/.*/GIT_AUTHOR_NAME='\''&'\''/p

		g
		s/^author [^<]* <\([^>]*\)> .*$/\1/
		s/'\''/'\''\'\'\''/g
		s/.*/GIT_AUTHOR_EMAIL='\''&'\''/p

		g
		s/^author [^<]* <[^>]*> \(.*\)$/\1/
		s/'\''/'\''\'\'\''/g
		s/.*/GIT_AUTHOR_DATE='\''&'\''/p

		q
	}
	'
	set_author_env=`git-cat-file commit "$use_commit" |
	LANG=C LC_ALL=C sed -ne "$pick_author_script"`
	eval "$set_author_env"
	export GIT_AUTHOR_NAME
	export GIT_AUTHOR_EMAIL
	export GIT_AUTHOR_DATE
fi

PARENTS="-p HEAD"
if test -z "$initial_commit"
then
	if [ -f "$GIT_DIR/MERGE_HEAD" ]; then
		PARENTS="-p HEAD "`sed -e 's/^/-p /' "$GIT_DIR/MERGE_HEAD"`
	fi
else
	if [ -z "$(git-ls-files)" ]; then
		echo >&2 Nothing to commit
		exit 1
	fi
	PARENTS=""
fi


run_status >>"$GIT_DIR"/COMMIT_EDITMSG
if [ "$?" != "0" -a ! -f "$GIT_DIR/MERGE_HEAD" ]
then
	rm -f "$GIT_DIR/COMMIT_EDITMSG"
	run_status
	exit 1
fi
case "$no_edit" in
'')
	case "${VISUAL:-$EDITOR},$TERM" in
	,dumb)
		echo >&2 "Terminal is dumb but no VISUAL nor EDITOR defined."
		echo >&2 "Please supply the commit log message using either"
		echo >&2 "-m or -F option.  A boilerplate log message has"
		echo >&2 "been prepared in $GIT_DIR/COMMIT_EDITMSG"
		exit 1
		;;
	esac
	${VISUAL:-${EDITOR:-vi}} "$GIT_DIR/COMMIT_EDITMSG"
	;;
esac

case "$verify" in
t)
	if test -x "$GIT_DIR"/hooks/commit-msg
	then
		"$GIT_DIR"/hooks/commit-msg "$GIT_DIR"/COMMIT_EDITMSG || exit
	fi
esac

grep -v '^#' < "$GIT_DIR"/COMMIT_EDITMSG |
git-stripspace > "$GIT_DIR"/COMMIT_MSG

if cnt=`grep -v -i '^Signed-off-by' "$GIT_DIR"/COMMIT_MSG |
	git-stripspace |
	wc -l` &&
   test 0 -lt $cnt
then
	if test -z "$TMP_INDEX"
	then
		tree=$(git-write-tree)
	else
		tree=$(GIT_INDEX_FILE="$TMP_INDEX" git-write-tree) &&
		rm -f "$TMP_INDEX"
	fi &&
	commit=$(cat "$GIT_DIR"/COMMIT_MSG | git-commit-tree $tree $PARENTS) &&
	git-update-ref HEAD $commit $current &&
	rm -f -- "$GIT_DIR/MERGE_HEAD"
else
	echo >&2 "* no commit message?  aborting commit."
	false
fi
ret="$?"
rm -f "$GIT_DIR/COMMIT_MSG" "$GIT_DIR/COMMIT_EDITMSG"
git-rerere

if test -x "$GIT_DIR"/hooks/post-commit && test "$ret" = 0
then
	"$GIT_DIR"/hooks/post-commit
fi
if test 0 -eq "$ret"
then
	rm -f "$SAVE_INDEX"
fi
exit "$ret"
