#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#

OPTIONS_KEEPDASHDASH=
OPTIONS_SPEC="\
git repack [options]
--
a               pack everything in a single pack
A               same as -a, and turn unreachable objects loose
d               remove redundant packs, and run git-prune-packed
f               pass --no-reuse-delta to git-pack-objects
F               pass --no-reuse-object to git-pack-objects
n               do not run git-update-server-info
q,quiet         be quiet
l               pass --local to git-pack-objects
 Packing constraints
window=         size of the window used for delta compression
window-memory=  same as the above, but limit memory size instead of entries count
depth=          limits the maximum delta depth
max-pack-size=  maximum size of each packfile
"
SUBDIRECTORY_OK='Yes'
. git-sh-setup

no_update_info= all_into_one= remove_redundant= unpack_unreachable=
local= no_reuse= extra=
while test $# != 0
do
	case "$1" in
	-n)	no_update_info=t ;;
	-a)	all_into_one=t ;;
	-A)	all_into_one=t
		unpack_unreachable=--unpack-unreachable ;;
	-d)	remove_redundant=t ;;
	-q)	GIT_QUIET=t ;;
	-f)	no_reuse=--no-reuse-delta ;;
	-F)	no_reuse=--no-reuse-object ;;
	-l)	local=--local ;;
	--max-pack-size|--window|--window-memory|--depth)
		extra="$extra $1=$2"; shift ;;
	--) shift; break;;
	*)	usage ;;
	esac
	shift
done

case "`git config --bool repack.usedeltabaseoffset || echo true`" in
true)
	extra="$extra --delta-base-offset" ;;
esac

PACKDIR="$GIT_OBJECT_DIRECTORY/pack"
PACKTMP="$PACKDIR/.tmp-$$-pack"
rm -f "$PACKTMP"-*
trap 'rm -f "$PACKTMP"-*' 0 1 2 3 15

# There will be more repacking strategies to come...
case ",$all_into_one," in
,,)
	args='--unpacked --incremental'
	;;
,t,)
	args= existing=
	if [ -d "$PACKDIR" ]; then
		for e in `cd "$PACKDIR" && find . -type f -name '*.pack' \
			| sed -e 's/^\.\///' -e 's/\.pack$//'`
		do
			if [ -e "$PACKDIR/$e.keep" ]; then
				: keep
			else
				existing="$existing $e"
			fi
		done
		if test -n "$existing" -a -n "$unpack_unreachable" -a \
			-n "$remove_redundant"
		then
			args="$args $unpack_unreachable"
		fi
	fi
	;;
esac

mkdir -p "$PACKDIR" || exit

args="$args $local ${GIT_QUIET:+-q} $no_reuse$extra"
names=$(git pack-objects --keep-true-parents --honor-pack-keep --non-empty --all --reflog $args </dev/null "$PACKTMP") ||
	exit 1
if [ -z "$names" ]; then
	say Nothing new to pack.
fi

# Ok we have prepared all new packfiles.

# First see if there are packs of the same name and if so
# if we can move them out of the way (this can happen if we
# repacked immediately after packing fully.
rollback=
failed=
for name in $names
do
	for sfx in pack idx
	do
		file=pack-$name.$sfx
		test -f "$PACKDIR/$file" || continue
		rm -f "$PACKDIR/old-$file" &&
		mv "$PACKDIR/$file" "$PACKDIR/old-$file" || {
			failed=t
			break
		}
		rollback="$rollback $file"
	done
	test -z "$failed" || break
done

# If renaming failed for any of them, roll the ones we have
# already renamed back to their original names.
if test -n "$failed"
then
	rollback_failure=
	for file in $rollback
	do
		mv "$PACKDIR/old-$file" "$PACKDIR/$file" ||
		rollback_failure="$rollback_failure $file"
	done
	if test -n "$rollback_failure"
	then
		echo >&2 "WARNING: Some packs in use have been renamed by"
		echo >&2 "WARNING: prefixing old- to their name, in order to"
		echo >&2 "WARNING: replace them with the new version of the"
		echo >&2 "WARNING: file.  But the operation failed, and"
		echo >&2 "WARNING: attempt to rename them back to their"
		echo >&2 "WARNING: original names also failed."
		echo >&2 "WARNING: Please rename them in $PACKDIR manually:"
		for file in $rollback_failure
		do
			echo >&2 "WARNING:   old-$file -> $file"
		done
	fi
	exit 1
fi

# Now the ones with the same name are out of the way...
fullbases=
for name in $names
do
	fullbases="$fullbases pack-$name"
	chmod a-w "$PACKTMP-$name.pack"
	chmod a-w "$PACKTMP-$name.idx"
	mv -f "$PACKTMP-$name.pack" "$PACKDIR/pack-$name.pack" &&
	mv -f "$PACKTMP-$name.idx"  "$PACKDIR/pack-$name.idx" ||
	exit
done

# Remove the "old-" files
for name in $names
do
	rm -f "$PACKDIR/old-pack-$name.idx"
	rm -f "$PACKDIR/old-pack-$name.pack"
done

# End of pack replacement.

if test "$remove_redundant" = t
then
	# We know $existing are all redundant.
	if [ -n "$existing" ]
	then
		( cd "$PACKDIR" &&
		  for e in $existing
		  do
			case " $fullbases " in
			*" $e "*) ;;
			*)	rm -f "$e.pack" "$e.idx" "$e.keep" ;;
			esac
		  done
		)
	fi
	git prune-packed ${GIT_QUIET:+-q}
fi

case "$no_update_info" in
t) : ;;
*) git update-server-info ;;
esac
