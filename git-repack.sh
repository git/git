#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#

USAGE='[-a] [-d] [-f] [-l] [-n] [-q]'
. git-sh-setup

no_update_info= all_into_one= remove_redundant=
local= quiet= no_reuse_delta= extra=
while case "$#" in 0) break ;; esac
do
	case "$1" in
	-n)	no_update_info=t ;;
	-a)	all_into_one=t ;;
	-d)	remove_redundant=t ;;
	-q)	quiet=-q ;;
	-f)	no_reuse_delta=--no-reuse-delta ;;
	-l)	local=--local ;;
	--window=*) extra="$extra $1" ;;
	--depth=*) extra="$extra $1" ;;
	*)	usage ;;
	esac
	shift
done

PACKDIR="$GIT_OBJECT_DIRECTORY/pack"
PACKTMP="$GIT_DIR/.tmp-$$-pack"
rm -f "$PACKTMP"-*
trap 'rm -f "$PACKTMP"-*' 0 1 2 3 15

# There will be more repacking strategies to come...
case ",$all_into_one," in
,,)
	rev_list='--unpacked'
	pack_objects='--incremental'
	;;
,t,)
	rev_list=
	pack_objects=

	# Redundancy check in all-into-one case is trivial.
	existing=`test -d "$PACKDIR" && cd "$PACKDIR" && \
	    find . -type f \( -name '*.pack' -o -name '*.idx' \) -print`
	;;
esac

pack_objects="$pack_objects $local $quiet $no_reuse_delta$extra"
name=$( { git-rev-list --objects --all $rev_list ||
	  echo "git-rev-list died with exit code $?"
	} |
	git-pack-objects --non-empty $pack_objects "$PACKTMP") ||
	exit 1
if [ -z "$name" ]; then
	echo Nothing new to pack.
else
	if test "$quiet" != '-q'; then
	    echo "Pack pack-$name created."
	fi
	mkdir -p "$PACKDIR" || exit

	for sfx in pack idx
	do
		if test -f "$PACKDIR/pack-$name.$sfx"
		then
			mv -f "$PACKDIR/pack-$name.$sfx" \
				"$PACKDIR/old-pack-$name.$sfx"
		fi
	done &&
	mv -f "$PACKTMP-$name.pack" "$PACKDIR/pack-$name.pack" &&
	mv -f "$PACKTMP-$name.idx"  "$PACKDIR/pack-$name.idx" &&
	test -f "$PACKDIR/pack-$name.pack" &&
	test -f "$PACKDIR/pack-$name.idx" || {
		echo >&2 "Couldn't replace the existing pack with updated one."
		echo >&2 "The original set of packs have been saved as"
		echo >&2 "old-pack-$name.{pack,idx} in $PACKDIR."
		exit 1
	}
	rm -f "$PACKDIR/old-pack-$name.pack" "$PACKDIR/old-pack-$name.idx"
fi

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
	git-prune-packed
fi

case "$no_update_info" in
t) : ;;
*) git-update-server-info ;;
esac
