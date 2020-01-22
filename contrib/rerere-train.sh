#!/bin/sh
# Copyright (c) 2008, Nanako Shiraishi
# Prime rerere database from existing merge commits

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

. "$(git --exec-path)/git-sh-setup"
require_work_tree
cd_to_toplevel

# Remember original branch
branch=$(git symbolic-ref -q HEAD) ||
original_HEAD=$(git rev-parse --verify HEAD) || {
	echo >&2 "Not on any branch and no commit yet?"
	exit 1
}

mkdir -p "$GIT_DIR/rr-cache" || exit

git rev-list --parents "$@" |
while read commit parent1 other_parents
do
	if test -z "$other_parents"
	then
		# Skip non-merges
		continue
	fi
	git checkout -q "$parent1^0"
	if git merge $other_parents >/dev/null 2>&1
	then
		# Cleanly merges
		continue
	fi
	if test $overwrite = 1
	then
		git rerere forget .
	fi
	if test -s "$GIT_DIR/MERGE_RR"
	then
		git show -s --pretty=format:"Learning from %h %s" "$commit"
		git rerere
		git checkout -q $commit -- .
		git rerere
	fi
	git reset -q --hard
done

if test -z "$branch"
then
	git checkout "$original_HEAD"
else
	git checkout "${branch#refs/heads/}"
fi
