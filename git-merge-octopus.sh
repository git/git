#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#
# Resolve two or more trees.
#

LF='
'

die () {
    echo >&2 "$*"
    exit 1
}

# The first parameters up to -- are merge bases; the rest are heads.
bases= head= remotes= sep_seen=
for arg
do
	case ",$sep_seen,$head,$arg," in
	*,--,)
		sep_seen=yes
		;;
	,yes,,*)
		head=$arg
		;;
	,yes,*)
		remotes="$remotes$arg "
		;;
	*)
		bases="$bases$arg "
		;;
	esac
done

# Reject if this is not an Octopus -- resolve should be used instead.
case "$remotes" in
?*' '?*)
	;;
*)
	exit 2 ;;
esac

# MRC is the current "merge reference commit"
# MRT is the current "merge result tree"

MRC=$head MSG= PARENT="-p $head"
MRT=$(git write-tree)
CNT=1 ;# counting our head
NON_FF_MERGE=0
OCTOPUS_FAILURE=0
for SHA1 in $remotes
do
	case "$OCTOPUS_FAILURE" in
	1)
		# We allow only last one to have a hand-resolvable
		# conflicts.  Last round failed and we still had
		# a head to merge.
		echo "Automated merge did not work."
		echo "Should not be doing an Octopus."
		exit 2
	esac

	common=$(git merge-base --all $MRC $SHA1) ||
		die "Unable to find common commit with $SHA1"

	case "$LF$common$LF" in
	*"$LF$SHA1$LF"*)
		echo "Already up-to-date with $SHA1"
		continue
		;;
	esac

	CNT=`expr $CNT + 1`
	PARENT="$PARENT -p $SHA1"

	if test "$common,$NON_FF_MERGE" = "$MRC,0"
	then
		# The first head being merged was a fast-forward.
		# Advance MRC to the head being merged, and use that
		# tree as the intermediate result of the merge.
		# We still need to count this as part of the parent set.

		echo "Fast forwarding to: $SHA1"
		git read-tree -u -m $head $SHA1 || exit
		MRC=$SHA1 MRT=$(git write-tree)
		continue
	fi

	NON_FF_MERGE=1

	echo "Trying simple merge with $SHA1"
	git read-tree -u -m --aggressive  $common $MRT $SHA1 || exit 2
	next=$(git write-tree 2>/dev/null)
	if test $? -ne 0
	then
		echo "Simple merge did not work, trying automatic merge."
		git-merge-index -o git-merge-one-file -a ||
		OCTOPUS_FAILURE=1
		next=$(git write-tree 2>/dev/null)
	fi

	# We have merged the other branch successfully.  Ideally
	# we could implement OR'ed heads in merge-base, and keep
	# a list of commits we have merged so far in MRC to feed
	# them to merge-base, but we approximate it by keep using
	# the current MRC.  We used to update it to $common, which
	# was incorrectly doing AND'ed merge-base here, which was
	# unneeded.

	MRT=$next
done

exit "$OCTOPUS_FAILURE"
