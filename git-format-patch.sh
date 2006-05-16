#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

USAGE='[-n | -k] [-o <dir> | --stdout] [--signoff] [--check] [--diff-options] [--attach] <his> [<mine>]'
LONG_USAGE='Prepare each commit with its patch since <mine> head forked from
<his> head, one file per patch formatted to resemble UNIX mailbox
format, for e-mail submission or use with git-am.

Each output file is numbered sequentially from 1, and uses the
first line of the commit message (massaged for pathname safety)
as the filename.

When -o is specified, output files are created in <dir>; otherwise
they are created in the current working directory.  This option
is ignored if --stdout is specified.

When -n is specified, instead of "[PATCH] Subject", the first
line is formatted as "[PATCH N/M] Subject", unless you have only
one patch.

When --attach is specified, patches are attached, not inlined.'

. git-sh-setup

# Force diff to run in C locale.
LANG=C LC_ALL=C
export LANG LC_ALL

diff_opts=
LF='
'

outdir=./
while case "$#" in 0) break;; esac
do
    case "$1" in
    -c|--c|--ch|--che|--chec|--check)
    check=t ;;
    -a|--a|--au|--aut|--auth|--autho|--author|\
    -d|--d|--da|--dat|--date|\
    -m|--m|--mb|--mbo|--mbox) # now noop
    ;;
    --at|--att|--atta|--attac|--attach)
    attach=t ;;
    -k|--k|--ke|--kee|--keep|--keep-|--keep-s|--keep-su|--keep-sub|\
    --keep-subj|--keep-subje|--keep-subjec|--keep-subject)
    keep_subject=t ;;
    -n|--n|--nu|--num|--numb|--numbe|--number|--numbere|--numbered)
    numbered=t ;;
    -s|--s|--si|--sig|--sign|--signo|--signof|--signoff)
    signoff=t ;;
    --st|--std|--stdo|--stdou|--stdout)
    stdout=t ;;
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
		rev1=`expr "z$revpair" : 'z\(.*\)\.\.'`
		rev2=`expr "z$revpair" : 'z.*\.\.\(.*\)'`
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
headers=`git-repo-config --get format.headers`
case "$attach" in
"") ;;
*)
	mimemagic="050802040500080604070107"
esac

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

process_one () {
	perl -w -e '
my ($keep_subject, $num, $signoff, $headers, $mimemagic, $commsg) = @ARGV;
my ($signoff_pattern, $done_header, $done_subject, $done_separator, $signoff_seen,
    $last_was_signoff);

if ($signoff) {
	$signoff = "Signed-off-by: " . `git-var GIT_COMMITTER_IDENT`;
	$signoff =~ s/>.*/>/;
	$signoff_pattern = quotemeta($signoff);
}

my @weekday_names = qw(Sun Mon Tue Wed Thu Fri Sat);
my @month_names = qw(Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec);

sub show_date {
    my ($time, $tz) = @_;
    my $minutes = abs($tz);
    $minutes = int($minutes / 100) * 60 + ($minutes % 100);
    if ($tz < 0) {
        $minutes = -$minutes;
    }
    my $t = $time + $minutes * 60;
    my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday) = gmtime($t);
    return sprintf("%s, %d %s %d %02d:%02d:%02d %+05d",
		   $weekday_names[$wday], $mday,
		   $month_names[$mon], $year+1900,
		   $hour, $min, $sec, $tz);
}

print "From nobody Mon Sep 17 00:00:00 2001\n";
open FH, "git stripspace <$commsg |" or die "open $commsg pipe";
while (<FH>) {
    unless ($done_header) {
	if (/^$/) {
	    $done_header = 1;
	}
	elsif (/^author (.*>) (.*)$/) {
	    my ($author_ident, $author_date) = ($1, $2);
	    my ($utc, $off) = ($author_date =~ /^(\d+) ([-+]?\d+)$/);
	    $author_date = show_date($utc, $off);

	    print "From: $author_ident\n";
	    print "Date: $author_date\n";
	}
	next;
    }
    unless ($done_subject) {
	unless ($keep_subject) {
	    s/^\[PATCH[^]]*\]\s*//;
	    s/^/[PATCH$num] /;
	}
	if ($headers) {
	    print "$headers\n";
	}
        print "Subject: $_";
	if ($mimemagic) {
	    print "MIME-Version: 1.0\n";
	    print "Content-Type: multipart/mixed;\n";
	    print " boundary=\"------------$mimemagic\"\n";
	    print "\n";
	    print "This is a multi-part message in MIME format.\n";
	    print "--------------$mimemagic\n";
	    print "Content-Type: text/plain; charset=UTF-8; format=fixed\n";
	    print "Content-Transfer-Encoding: 8bit\n";
	}
	$done_subject = 1;
	next;
    }
    unless ($done_separator) {
        print "\n";
        $done_separator = 1;
        next if (/^$/);
    }

    $last_was_signoff = 0;
    if (/Signed-off-by:/i) {
        if ($signoff ne "" && /Signed-off-by:\s*$signoff_pattern$/i) {
	    $signoff_seen = 1;
	}
    }
    print $_;
}
if (!$signoff_seen && $signoff ne "") {
    if (!$last_was_signoff) {
        print "\n";
    }
    print "$signoff\n";
}
print "\n---\n\n";
close FH or die "close $commsg pipe";
' "$keep_subject" "$num" "$signoff" "$headers" "$mimemagic" $commsg

	git-diff-tree -p --stat --summary $diff_opts "$commit"
	echo
	case "$mimemagic" in
	'');;
	*)
		echo "--------------$mimemagic"
		echo "Content-Type: text/x-patch;"
		echo " name=\"$commit.diff\""
		echo "Content-Transfer-Encoding: 8bit"
		echo "Content-Disposition: inline;"
		echo " filename=\"$commit.diff\""
		echo
	esac
	git-diff-tree -p $diff_opts "$commit"
	case "$mimemagic" in
	'')
		echo "-- "
		echo "@@GIT_VERSION@@"
		;;
	*)
		echo
		echo "--------------$mimemagic--"
		echo
		;;
	esac
	echo
}

total=`wc -l <$series | tr -dc "[0-9]"`
case "$total,$numbered" in
1,*)
	numfmt='' ;;
*,t)
	numfmt=`echo "$total" | wc -c`
	numfmt=$(($numfmt-1))
	numfmt=" %0${numfmt}d/$total"
esac

i=1
while read commit
do
    git-cat-file commit "$commit" | git-stripspace >$commsg
    title=`sed -ne "$titleScript" <$commsg`
    case "$numbered" in
    '') num= ;;
    *)
        num=`printf "$numfmt" $i` ;;
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
