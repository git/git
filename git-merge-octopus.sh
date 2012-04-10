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

MRC=$(git rev-parse --verify -q $head)
MRT=$(git write-tree)
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

	eval pretty_name=\${GITHEAD_$SHA1:-$SHA1}
	if test "$SHA1" = "$pretty_name"
	then
		SHA1_UP="$(echo "$SHA1" | tr a-z A-Z)"
		eval pretty_name=\${GITHEAD_$SHA1_UP:-$pretty_name}
	fi
	common=$(git merge-base --all $SHA1 $MRC) ||
		die "Unable to find common commit with $pretty_name"

	case "$LF$common$LF" in
	*"$LF$SHA1$LF"*)
		cat << EOF
Already up-to-date with $pretty_name
EOF
		continue
		;;
	esac

	if test "$common,$NON_FF_MERGE" = "$MRC,0"
	then
		# The first head being merged was a fast-forward.
		# Advance MRC to the head being merged, and use that
		# tree as the intermediate result of the merge.
		# We still need to count this as part of the parent set.

		cat << EOF
Fast-forwarding to: $pretty_name
EOF
		git read-tree -u -m $head $SHA1 || exit
		MRC=$SHA1 MRT=$(git write-tree)
		continue
	fi

	NON_FF_MERGE=1

	cat << EOF
Trying simple merge with $pretty_name
EOF
	git read-tree -u -m --aggressive  $common $MRT $SHA1 || exit 2
	next=$(git write-tree 2>/dev/null)
	if test $? -ne 0
	then
		echo "Simple merge did not work, trying automatic merge."
		git-merge-index -o git-merge-one-file -a ||
		OCTOPUS_FAILURE=1
		next=$(git write-tree 2>/dev/null)
	fi

	MRC="$MRC $SHA1"
	MRT=$next
done

exit "$OCTOPUS_FAILURE"
