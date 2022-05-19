#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#
# Resolve two or more trees.
#

. but-sh-setup

LF='
'

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

# Reject if this is not an octopus -- resolve should be used instead.
case "$remotes" in
?*' '?*)
	;;
*)
	exit 2 ;;
esac

# MRC is the current "merge reference cummit"
# MRT is the current "merge result tree"

if ! but diff-index --quiet --cached HEAD --
then
    gettextln "Error: Your local changes to the following files would be overwritten by merge"
    but diff-index --cached --name-only HEAD -- | sed -e 's/^/    /'
    exit 2
fi
MRC=$(but rev-parse --verify -q $head)
MRT=$(but write-tree)
NON_FF_MERGE=0
OCTOPUS_FAILURE=0
for SHA1 in $remotes
do
	case "$OCTOPUS_FAILURE" in
	1)
		# We allow only last one to have a hand-resolvable
		# conflicts.  Last round failed and we still had
		# a head to merge.
		gettextln "Automated merge did not work."
		gettextln "Should not be doing an octopus."
		exit 2
	esac

	eval pretty_name=\${BUTHEAD_$SHA1:-$SHA1}
	if test "$SHA1" = "$pretty_name"
	then
		SHA1_UP="$(echo "$SHA1" | tr a-z A-Z)"
		eval pretty_name=\${BUTHEAD_$SHA1_UP:-$pretty_name}
	fi
	common=$(but merge-base --all $SHA1 $MRC) ||
		die "$(eval_gettext "Unable to find common cummit with \$pretty_name")"

	case "$LF$common$LF" in
	*"$LF$SHA1$LF"*)
		eval_gettextln "Already up to date with \$pretty_name"
		continue
		;;
	esac

	if test "$common,$NON_FF_MERGE" = "$MRC,0"
	then
		# The first head being merged was a fast-forward.
		# Advance MRC to the head being merged, and use that
		# tree as the intermediate result of the merge.
		# We still need to count this as part of the parent set.

		eval_gettextln "Fast-forwarding to: \$pretty_name"
		but read-tree -u -m $head $SHA1 || exit
		MRC=$SHA1 MRT=$(but write-tree)
		continue
	fi

	NON_FF_MERGE=1

	eval_gettextln "Trying simple merge with \$pretty_name"
	but read-tree -u -m --aggressive  $common $MRT $SHA1 || exit 2
	next=$(but write-tree 2>/dev/null)
	if test $? -ne 0
	then
		gettextln "Simple merge did not work, trying automatic merge."
		but merge-index -o but-merge-one-file -a ||
		OCTOPUS_FAILURE=1
		next=$(but write-tree 2>/dev/null)
	fi

	MRC="$MRC $SHA1"
	MRT=$next
done

exit "$OCTOPUS_FAILURE"
