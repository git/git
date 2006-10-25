#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

USAGE='[-v] <upstream> [<head>] [<limit>]'
LONG_USAGE='             __*__*__*__*__> <upstream>
            /
  fork-point
            \__+__+__+__+__+__+__+__> <head>

Each commit between the fork-point (or <limit> if given) and <head> is
examined, and compared against the change each commit between the
fork-point and <upstream> introduces.  If the change seems to be in
the upstream, it is shown on the standard output with prefix "-".
Otherwise it is shown with prefix "+".'
. git-sh-setup

case "$1" in -v) verbose=t; shift ;; esac 

case "$#,$1" in
1,*..*)
    upstream=$(expr "z$1" : 'z\(.*\)\.\.') ours=$(expr "z$1" : '.*\.\.\(.*\)$')
    set x "$upstream" "$ours"
    shift ;;
esac

case "$#" in
1) upstream=`git-rev-parse --verify "$1"` &&
   ours=`git-rev-parse --verify HEAD` || exit
   limit="$upstream"
   ;;
2) upstream=`git-rev-parse --verify "$1"` &&
   ours=`git-rev-parse --verify "$2"` || exit
   limit="$upstream"
   ;;
3) upstream=`git-rev-parse --verify "$1"` &&
   ours=`git-rev-parse --verify "$2"` &&
   limit=`git-rev-parse --verify "$3"` || exit
   ;;
*) usage ;;
esac

# Note that these list commits in reverse order;
# not that the order in inup matters...
inup=`git-rev-list ^$ours $upstream` &&
ours=`git-rev-list $ours ^$limit` || exit

tmp=.cherry-tmp$$
patch=$tmp-patch
mkdir $patch
trap "rm -rf $tmp-*" 0 1 2 3 15

for c in $inup
do
	git-diff-tree -p $c
done | git-patch-id |
while read id name
do
	echo $name >>$patch/$id
done

LF='
'

O=
for c in $ours
do
	set x `git-diff-tree -p $c | git-patch-id`
	if test "$2" != ""
	then
		if test -f "$patch/$2"
		then
			sign=-
		else
			sign=+
		fi
		case "$verbose" in
		t)
			c=$(git-rev-list --pretty=oneline --max-count=1 $c)
		esac
		case "$O" in
		'')	O="$sign $c" ;;
		*)	O="$sign $c$LF$O" ;;
		esac
	fi
done
case "$O" in
'') ;;
*)  echo "$O" ;;
esac
