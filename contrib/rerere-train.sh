#!/bin/sh
# Copyright (c) 2008, Nanako Shiraishi
# Prime rerere database from existing merge cummits

me=rerere-train
USAGE=$(cat <<-EOF
usage: $me [--overwrite] <rev-list-args>

    -h, --help            show the help
    -o, --overwrite       overwrite any existing rerere cache
EOF
)

SUBDIRECTORY_OK=Yes

overwrite=0

while test $# -gt 0
do
	opt="$1"
	case "$opt" in
	-h|--help)
		echo "$USAGE"
		exit 0
		;;
	-o|--overwrite)
		overwrite=1
		shift
		break
		;;
	--)
		shift
		break
		;;
	*)
		break
		;;
	esac
done

# Overwrite or help options are not valid except as first arg
for opt in "$@"
do
	case "$opt" in
	-h|--help)
		echo "$USAGE"
		exit 0
		;;
	-o|--overwrite)
		echo "$USAGE"
		exit 0
		;;
	esac
done

. "$(but --exec-path)/but-sh-setup"
require_work_tree
cd_to_toplevel

# Remember original branch
branch=$(but symbolic-ref -q HEAD) ||
original_HEAD=$(but rev-parse --verify HEAD) || {
	echo >&2 "Not on any branch and no cummit yet?"
	exit 1
}

mkdir -p "$GIT_DIR/rr-cache" || exit

but rev-list --parents "$@" |
while read cummit parent1 other_parents
do
	if test -z "$other_parents"
	then
		# Skip non-merges
		continue
	fi
	but checkout -q "$parent1^0"
	if but merge $other_parents >/dev/null 2>&1
	then
		# Cleanly merges
		continue
	fi
	if test $overwrite = 1
	then
		but rerere forget .
	fi
	if test -s "$GIT_DIR/MERGE_RR"
	then
		but --no-pager show -s --format="Learning from %h %s" "$cummit"
		but rerere
		but checkout -q $cummit -- .
		but rerere
	fi
	but reset -q --hard  # Might nuke untracked files...
done

if test -z "$branch"
then
	but checkout "$original_HEAD"
else
	but checkout "${branch#refs/heads/}"
fi
