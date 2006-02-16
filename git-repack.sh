#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#

USAGE='[-a] [-d] [-f] [-l] [-n] [-q]'
. git-sh-setup
	
no_update_info= all_into_one= remove_redundant=
local= quiet= no_reuse_delta=
while case "$#" in 0) break ;; esac
do
	case "$1" in
	-n)	no_update_info=t ;;
	-a)	all_into_one=t ;;
	-d)	remove_redundant=t ;;
	-q)	quiet=-q ;;
	-f)	no_reuse_delta=--no-reuse-delta ;;
	-l)	local=--local ;;
	*)	usage ;;
	esac
	shift
done

rm -f .tmp-pack-*
PACKDIR="$GIT_OBJECT_DIRECTORY/pack"

# There will be more repacking strategies to come...
case ",$all_into_one," in
,,)
	rev_list='--unpacked'
	rev_parse='--all'
	pack_objects='--incremental'
	;;
,t,)
	rev_list=
	rev_parse='--all'
	pack_objects=

	# Redundancy check in all-into-one case is trivial.
	existing=`cd "$PACKDIR" && \
	    find . -type f \( -name '*.pack' -o -name '*.idx' \) -print`
	;;
esac
pack_objects="$pack_objects $local $quiet $no_reuse_delta"
name=$(git-rev-list --objects $rev_list $(git-rev-parse $rev_parse) 2>&1 |
	git-pack-objects --non-empty $pack_objects .tmp-pack) ||
	exit 1
if [ -z "$name" ]; then
	echo Nothing new to pack.
	exit 0
fi
echo "Pack pack-$name created."

mkdir -p "$PACKDIR" || exit

mv .tmp-pack-$name.pack "$PACKDIR/pack-$name.pack" &&
mv .tmp-pack-$name.idx  "$PACKDIR/pack-$name.idx" ||
exit

if test "$remove_redundant" = t
then
	# We know $existing are all redundant only when
	# all-into-one is used.
	if test "$all_into_one" != '' && test "$existing" != ''
	then
		sync
		( cd "$PACKDIR" &&
		  for e in $existing
		  do
			case "$e" in
			./pack-$name.pack | ./pack-$name.idx) ;;
			*)	rm -f $e ;;
			esac
		  done
		)
	fi
fi

case "$no_update_info" in
t) : ;;
*) git-update-server-info ;;
esac
