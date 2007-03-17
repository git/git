#!/bin/sh
##
## "dotest" is my stupid name for my patch-application script, which
## I never got around to renaming after I tested it. We're now on the
## second generation of scripts, still called "dotest".
##
## Update: Ryan Anderson finally shamed me into naming this "applymbox".
##
## You give it a mbox-format collection of emails, and it will try to
## apply them to the kernel using "applypatch"
##
## The patch application may fail in the middle.  In which case:
## (1) look at .dotest/patch and fix it up to apply
## (2) re-run applymbox with -c .dotest/msg-number for the current one.
## Pay a special attention to the commit log message if you do this and
## use a Signoff_file, because applypatch wants to append the sign-off
## message to msg-clean every time it is run.
##
## git-am is supposed to be the newer and better tool for this job.

USAGE='[-u] [-k] [-q] [-m] (-c .dotest/<num> | mbox) [signoff]'
. git-sh-setup

git var GIT_COMMITTER_IDENT >/dev/null || exit

keep_subject= query_apply= continue= utf8=-u resume=t
while case "$#" in 0) break ;; esac
do
	case "$1" in
	-u)	utf8=-u ;;
	-n)	utf8=-n ;;
	-k)	keep_subject=-k ;;
	-q)	query_apply=t ;;
	-c)	continue="$2"; resume=f; shift ;;
	-m)	fall_back_3way=t ;;
	-*)	usage ;;
	*)	break ;;
	esac
	shift
done

case "$continue" in
'')
	rm -rf .dotest
	mkdir .dotest
	num_msgs=$(git-mailsplit "$1" .dotest) || exit 1
	echo "$num_msgs patch(es) to process."
	shift
esac

files=$(git-diff-index --cached --name-only HEAD) || exit
if [ "$files" ]; then
   echo "Dirty index: cannot apply patches (dirty: $files)" >&2
   exit 1
fi

case "$query_apply" in
t)	touch .dotest/.query_apply
esac
case "$fall_back_3way" in
t)	: >.dotest/.3way
esac
case "$keep_subject" in
-k)	: >.dotest/.keep_subject
esac

signoff="$1"
set x .dotest/0*
shift
while case "$#" in 0) break;; esac
do
    i="$1" 
    case "$resume,$continue" in
    f,$i)	resume=t;;
    f,*)	shift
		continue;;
    *)
	    git-mailinfo $keep_subject $utf8 \
		.dotest/msg .dotest/patch <$i >.dotest/info || exit 1
	    test -s .dotest/patch || {
		echo "Patch is empty.  Was is split wrong?"
		exit 1
	    }
	    git-stripspace < .dotest/msg > .dotest/msg-clean
	    ;;
    esac
    while :; # for fixing up and retry
    do
	git-applypatch .dotest/msg-clean .dotest/patch .dotest/info "$signoff"
	case "$?" in
	0)
		# Remove the cleanly applied one to reduce clutter.
		rm -f .dotest/$i
		;;
	2)
		# 2 is a special exit code from applypatch to indicate that
	    	# the patch wasn't applied, but continue anyway 
		;;
	*)
		ret=$?
		if test -f .dotest/.query_apply
		then
			echo >&2 "* Patch failed."
			echo >&2 "* You could fix it up in your editor and"
			echo >&2 "  retry.  If you want to do so, say yes here"
			echo >&2 "  AFTER fixing .dotest/patch up."
			echo >&2 -n "Retry [y/N]? "
			read yesno
			case "$yesno" in
			[Yy]*)
				continue ;;
		        esac
		fi
		exit $ret
	esac
	break
    done
    shift
done
# return to pristine
rm -fr .dotest
