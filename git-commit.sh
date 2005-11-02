#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#

. git-sh-setup || die "Not a git archive"

usage () {
	die 'git commit [-a] [-s] [-v | --no-verify]  [-m <message> | -F <logfile> | (-C|-c) <commit>] [-e] [<path>...]'
}

all= logfile= use_commit= no_edit= log_given= log_message= verify=t signoff=
while case "$#" in 0) break;; esac
do
  case "$1" in
  -a|--a|--al|--all)
    all=t
    shift ;;
  -F=*|--f=*|--fi=*|--fil=*|--file=*)
    log_given=t$log_given
    logfile=`expr "$1" : '-[^=]*=\(.*\)'`
    no_edit=t
    shift ;;
  -F|--f|--fi|--fil|--file)
    case "$#" in 1) usage ;; esac; shift
    log_given=t$log_given
    logfile="$1"
    no_edit=t
    shift ;;
  -m=*|--m=*|--me=*|--mes=*|--mess=*|--messa=*|--messag=*|--message=*)
    log_given=t$log_given
    log_message=`expr "$1" : '-[^=]*=\(.*\)'`
    no_edit=t
    shift ;;
  -m|--m|--me|--mes|--mess|--messa|--messag|--message)
    case "$#" in 1) usage ;; esac; shift
    log_given=t$log_given
    log_message="$1"
    no_edit=t
    shift ;;
  -c=*|--ree=*|--reed=*|--reedi=*|--reedit=*|--reedit-=*|--reedit-m=*|\
  --reedit-me=*|--reedit-mes=*|--reedit-mess=*|--reedit-messa=*|\
  --reedit-messag=*|--reedit-message=*)
    log_given=t$log_given
    use_commit=`expr "$1" : '-[^=]*=\(.*\)'`
    shift ;;
  -c|--ree|--reed|--reedi|--reedit|--reedit-|--reedit-m|--reedit-me|\
  --reedit-mes|--reedit-mess|--reedit-messa|--reedit-messag|--reedit-message)
    case "$#" in 1) usage ;; esac; shift
    log_given=t$log_given
    use_commit="$1"
    shift ;;
  -C=*|--reu=*|--reus=*|--reuse=*|--reuse-=*|--reuse-m=*|--reuse-me=*|\
  --reuse-mes=*|--reuse-mess=*|--reuse-messa=*|--reuse-messag=*|\
  --reuse-message=*)
    log_given=t$log_given
    use_commit=`expr "$1" : '-[^=]*=\(.*\)'`
    no_edit=t
    shift ;;
  -C|--reu|--reus|--reuse|--reuse-|--reuse-m|--reuse-me|--reuse-mes|\
  --reuse-mess|--reuse-messa|--reuse-messag|--reuse-message)
    case "$#" in 1) usage ;; esac; shift
    log_given=t$log_given
    use_commit="$1"
    no_edit=t
    shift ;;
  -e|--e|--ed|--edi|--edit)
    no_edit=
    shift ;;
  -s|--s|--si|--sig|--sign|--signo|--signof|--signoff)
    signoff=t
    shift ;;
  -n|--n|--no|--no-|--no-v|--no-ve|--no-ver|--no-veri|--no-verif|--no-verify)
    verify=
    shift ;;
  -v|--v|--ve|--ver|--veri|--verif|--verify)
    verify=t
    shift ;;
  --)
    shift
    break ;;
  -*)
     usage ;;
  *)
    break ;;
  esac
done

case "$log_given" in
tt*)
  die "Only one of -c/-C/-F/-m can be used." ;;
esac

case "$all,$#" in
t,*)
	git-diff-files --name-only -z |
	git-update-index --remove -z --stdin
	;;
,0)
	;;
*)
	git-diff-files --name-only -z -- "$@" |
	git-update-index --remove -z --stdin
	;;
esac || exit 1
git-update-index -q --refresh || exit 1

case "$verify" in
t)
	if test -x "$GIT_DIR"/hooks/pre-commit
	then
		"$GIT_DIR"/hooks/pre-commit || exit
	fi
esac

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
	echo "# It looks like your may be committing a MERGE."
	echo "# If this is not correct, please remove the file"
	echo "#	$GIT_DIR/MERGE_HEAD"
	echo "# and try again"
	echo "#"
fi >>"$GIT_DIR"/COMMIT_EDITMSG

PARENTS="-p HEAD"
if GIT_DIR="$GIT_DIR" git-rev-parse --verify HEAD >/dev/null 2>&1
then
	if [ -f "$GIT_DIR/MERGE_HEAD" ]; then
		PARENTS="-p HEAD "`sed -e 's/^/-p /' "$GIT_DIR/MERGE_HEAD"`
	fi
	if test "$use_commit" != ""
	then
		pick_author_script='
		/^author /{
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
		sed -ne "$pick_author_script"`
		eval "$set_author_env"
		export GIT_AUTHOR_NAME
		export GIT_AUTHOR_EMAIL
		export GIT_AUTHOR_DATE
	fi
else
	if [ -z "$(git-ls-files)" ]; then
		echo Nothing to commit 1>&2
		exit 1
	fi
	PARENTS=""
fi
git-status >>"$GIT_DIR"/COMMIT_EDITMSG
if [ "$?" != "0" -a ! -f "$GIT_DIR/MERGE_HEAD" ]
then
	rm -f "$GIT_DIR/COMMIT_EDITMSG"
	git-status
	exit 1
fi
case "$no_edit" in
'')
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
	tree=$(git-write-tree) &&
	commit=$(cat "$GIT_DIR"/COMMIT_MSG | git-commit-tree $tree $PARENTS) &&
	git-update-ref HEAD $commit $current &&
	rm -f -- "$GIT_DIR/MERGE_HEAD"
else
	echo >&2 "* no commit message?  aborting commit."
	false
fi
ret="$?"
rm -f "$GIT_DIR/COMMIT_MSG" "$GIT_DIR/COMMIT_EDITMSG"

if test -x "$GIT_DIR"/hooks/post-commit && test "$ret" = 0
then
	"$GIT_DIR"/hooks/post-commit
fi
exit "$ret"
