#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

. git-sh-setup || die "Not a git archive."

usage () {
    echo >&2 "usage: $0"' [-n] [-o dir] [--keep-subject] [--mbox] [--check] [--signoff] [-<diff options>...] upstream [ our-head ]

Prepare each commit with its patch since our-head forked from upstream,
one file per patch, for e-mail submission.  Each output file is
numbered sequentially from 1, and uses the first line of the commit
message (massaged for pathname safety) as the filename.

When -o is specified, output files are created in that directory; otherwise in
the current working directory.

When -n is specified, instead of "[PATCH] Subject", the first line is formatted
as "[PATCH N/M] Subject", unless you have only one patch.

When --mbox is specified, the output is formatted to resemble
UNIX mailbox format, and can be concatenated together for processing
with applymbox.
'
    exit 1
}

diff_opts=
IFS='
'
LF='
'

outdir=./
while case "$#" in 0) break;; esac
do
    case "$1" in
    -a|--a|--au|--aut|--auth|--autho|--author)
    author=t ;;
    -c|--c|--ch|--che|--chec|--check)
    check=t ;;
    -d|--d|--da|--dat|--date)
    date=t ;;
    -m|--m|--mb|--mbo|--mbox)
    date=t author=t mbox=t ;;
    -k|--k|--ke|--kee|--keep|--keep-|--keep-s|--keep-su|--keep-sub|\
    --keep-subj|--keep-subje|--keep-subjec|--keep-subject)
    keep_subject=t ;;
    -n|--n|--nu|--num|--numb|--numbe|--number|--numbere|--numbered)
    numbered=t ;;
    -s|--s|--si|--sig|--sign|--signo|--signof|--signoff)
    signoff=t ;;
    -o=*|--o=*|--ou=*|--out=*|--outp=*|--outpu=*|--output=*|--output-=*|\
    --output-d=*|--output-di=*|--output-dir=*|--output-dire=*|\
    --output-direc=*|--output-direct=*|--output-directo=*|\
    --output-director=*|--output-directory=*)
    outdir=`expr "$1" : '-[^=]*=\(.*\)'` ;;
    -o|--o|--ou|--out|--outp|--outpu|--output|--output-|--output-d|\
    --output-di|--output-dir|--output-dire|--output-direc|--output-direct|\
    --output-directo|--output-director|--output-directory)
    case "$#" in 1) usage ;; esac; shift
    outdir="$1" ;;
    -*)	diff_opts="$diff_opts$LF$1" ;;
    *) break ;;
    esac
    shift
done

case "$keep_subject$numbered" in
tt)
	die '--keep-subject and --numbered are incompatible.' ;;
esac

revpair=
case "$#" in
2)
    revpair="$1..$2" ;;
1)
    case "$1" in
    *..*)
    	revpair="$1";;
    *)
	revpair="$1..HEAD";;
    esac ;;
*)
    usage ;;
esac

me=`git-var GIT_AUTHOR_IDENT | sed -e 's/>.*/>/'`

case "$outdir" in
*/) ;;
*) outdir="$outdir/" ;;
esac
test -d "$outdir" || mkdir -p "$outdir" || exit

tmp=.tmp-series$$
trap 'rm -f $tmp-*' 0 1 2 3 15

series=$tmp-series
commsg=$tmp-commsg
filelist=$tmp-files

titleScript='
	/./d
	/^$/n
	s/^\[PATCH[^]]*\] *//
	s/[^-a-z.A-Z_0-9]/-/g
        s/\.\.\.*/\./g
	s/\.*$//
	s/--*/-/g
	s/^-//
	s/-$//
	s/$/./
	p
	q
'

whosepatchScript='
/^author /{
	s/author \(.*>\) \(.*\)$/au='\''\1'\'' ad='\''\2'\''/p
	q
}'

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"
stripCommitHead='/^'"$_x40"' (from '"$_x40"')$/d'

git-rev-list --no-merges --merge-order \
	$(git-rev-parse --revs-only "$revpair") >$series
total=`wc -l <$series | tr -dc "[0-9]"`
i=$total
while read commit
do
    git-cat-file commit "$commit" | git-stripspace >$commsg
    title=`sed -ne "$titleScript" <$commsg`
    case "$numbered" in
    '') num= ;;
    *)
	case $total in
	1) num= ;;
	*) num=' '`printf "%d/%d" $i $total` ;;
	esac
    esac

    file=`printf '%04d-%stxt' $i "$title"`
    i=`expr "$i" - 1`
    echo "* $file"
    {
	mailScript='
	/./d
	/^$/n'
	case "$keep_subject" in
	t)  ;;
	*)
	    mailScript="$mailScript"'
	    s|^\[PATCH[^]]*\] *||
	    s|^|[PATCH'"$num"'] |'
	    ;;
	esac
	mailScript="$mailScript"'
	s|^|Subject: |'
	case "$mbox" in
	t)
	    echo 'From nobody Mon Sep 17 00:00:00 2001' ;# UNIX "From" line
	    ;;
	esac
	eval "$(sed -ne "$whosepatchScript" $commsg)"
	test "$author,$au" = ",$me" || {
		mailScript="$mailScript"'
	a\
From: '"$au"
	}
	test "$date,$au" = ",$me" || {
		mailScript="$mailScript"'
	a\
Date: '"$ad"
	}

	mailScript="$mailScript"'
	: body
	p
	n
	b body'

	sed -ne "$mailScript" <$commsg

	test "$signoff" = "t" && {
		offsigner=`git-var GIT_COMMITTER_IDENT | sed -e 's/>.*/>/'`
		line="Signed-off-by: $offsigner"
		grep -q "^$line\$" $commsg || {
			echo
			echo "$line"
			echo
		}
	}
	echo
	echo '---'
	echo
	git-diff-tree -p $diff_opts "$commit" | git-apply --stat --summary
	echo
	git-diff-tree -p $diff_opts "$commit" | sed -e "$stripCommitHead"

	case "$mbox" in
	t)
		echo
		;;
	esac
    } >"$outdir$file"
    case "$check" in
    t)
	# This is slightly modified from Andrew Morton's Perfect Patch.
	# Lines you introduce should not have trailing whitespace.
	# Also check for an indentation that has SP before a TAB.
        grep -n '^+\([ 	]* 	.*\|.*[ 	]\)$' "$outdir$file"

	: do not exit with non-zero because we saw no problem in the last one.
    esac
done <$series
