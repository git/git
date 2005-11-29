#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

. git-sh-setup

usage () {
    echo >&2 "usage: $0"' [-n] [-o dir | --stdout] [--keep-subject] [--mbox]
    [--check] [--signoff] [-<diff options>...]
    [--help]
    ( from..to ... | upstream [ our-head ] )

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
    --st|--std|--stdo|--stdou|--stdout)
    stdout=t mbox=t date=t author=t ;;
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
    -h|--h|--he|--hel|--help)
        usage
	;;
    -*' '* | -*"$LF"* | -*'	'*)
	# Ignore diff option that has whitespace for now.
	;;
    -*)	diff_opts="$diff_opts$1 " ;;
    *) break ;;
    esac
    shift
done

case "$keep_subject$numbered" in
tt)
	die '--keep-subject and --numbered are incompatible.' ;;
esac

tmp=.tmp-series$$
trap 'rm -f $tmp-*' 0 1 2 3 15

series=$tmp-series
commsg=$tmp-commsg
filelist=$tmp-files

# Backward compatible argument parsing hack.
#
# Historically, we supported:
# 1. "rev1"		is equivalent to "rev1..HEAD"
# 2. "rev1..rev2"
# 3. "rev1" "rev2	is equivalent to "rev1..rev2"
#
# We want to take a sequence of "rev1..rev2" in general.
# Also, "rev1.." should mean "rev1..HEAD"; git-diff users are
# familiar with that syntax.

case "$#,$1$2" in
1,?*..?*)
	# single "rev1..rev2"
	;;
1,?*..)
	# single "rev1.." should mean "rev1..HEAD"
	set x "$1"HEAD
	shift
	;;
1,*)
	# single rev1
	set x "$1..HEAD"
	shift
	;;
2,?*..?*)
	# not traditional "rev1" "rev2"
	;;
2,*)
	set x "$1..$2"
	shift
	;;
esac

# Now we have what we want in $@
for revpair
do
	case "$revpair" in
	?*..?*)
		rev1=`expr "$revpair" : '\(.*\)\.\.'`
		rev2=`expr "$revpair" : '.*\.\.\(.*\)'`
		;;
	*)
		rev1="$revpair^"
		rev2="$revpair"
		;;
	esac
	git-rev-parse --verify "$rev1^0" >/dev/null 2>&1 ||
		die "Not a valid rev $rev1 ($revpair)"
	git-rev-parse --verify "$rev2^0" >/dev/null 2>&1 ||
		die "Not a valid rev $rev2 ($revpair)"
	git-cherry -v "$rev1" "$rev2" |
	while read sign rev comment
	do
		case "$sign" in
		'-')
			echo >&2 "Merged already: $comment"
			;;
		*)
			echo $rev
			;;
		esac
	done
done >$series

me=`git-var GIT_AUTHOR_IDENT | sed -e 's/>.*/>/'`

case "$outdir" in
*/) ;;
*) outdir="$outdir/" ;;
esac
test -d "$outdir" || mkdir -p "$outdir" || exit

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

process_one () {
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

	eval "$(LANG=C LC_ALL=C sed -ne "$whosepatchScript" $commsg)"
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

	(cat $commsg ; echo; echo) |
	sed -ne "$mailScript" |
	git-stripspace

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
	git-diff-tree -p $diff_opts "$commit"
	echo "-- "
	echo "@@GIT_VERSION@@"

	case "$mbox" in
	t)
		echo
		;;
	esac
}

total=`wc -l <$series | tr -dc "[0-9]"`
i=1
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
    if test '' = "$stdout"
    then
	    echo "$file"
	    process_one >"$outdir$file"
	    if test t = "$check"
	    then
		# This is slightly modified from Andrew Morton's Perfect Patch.
		# Lines you introduce should not have trailing whitespace.
		# Also check for an indentation that has SP before a TAB.
		grep -n '^+\([ 	]* 	.*\|.*[ 	]\)$' "$outdir$file"
		:
	    fi
    else
	    echo >&2 "$file"
	    process_one
    fi
    i=`expr "$i" + 1`
done <$series
