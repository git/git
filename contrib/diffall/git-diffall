#!/bin/sh
# Copyright 2010 - 2012, Tim Henigan <tim.henigan@gmail.com>
#
# Perform a directory diff between commits in the repository using
# the external diff or merge tool specified in the user's config.

USAGE='[--cached] [--copy-back] [-x|--extcmd=<command>] <commit>{0,2} [-- <path>*]

    --cached     Compare to the index rather than the working tree.

    --copy-back  Copy files back to the working tree when the diff
                 tool exits (in case they were modified by the
                 user).  This option is only valid if the diff
                 compared with the working tree.

    -x=<command>
    --extcmd=<command>  Specify a custom command for viewing diffs.
                 git-diffall ignores the configured defaults and
                 runs $command $LOCAL $REMOTE when this option is
                 specified. Additionally, $BASE is set in the
                 environment.
'

SUBDIRECTORY_OK=1
. "$(git --exec-path)/git-sh-setup"

TOOL_MODE=diff
. "$(git --exec-path)/git-mergetool--lib"

merge_tool="$(get_merge_tool)"
if test -z "$merge_tool"
then
	echo "Error: Either the 'diff.tool' or 'merge.tool' option must be set."
	usage
fi

start_dir=$(pwd)

# All the file paths returned by the diff command are relative to the root
# of the working copy. So if the script is called from a subdirectory, it
# must switch to the root of working copy before trying to use those paths.
cdup=$(git rev-parse --show-cdup) &&
cd "$cdup" || {
	echo >&2 "Cannot chdir to $cdup, the toplevel of the working tree"
	exit 1
}

# set up temp dir
tmp=$(perl -e 'use File::Temp qw(tempdir);
	$t=tempdir("/tmp/git-diffall.XXXXX") or exit(1);
	print $t') || exit 1
trap 'rm -rf "$tmp"' EXIT

left=
right=
paths=
dashdash_seen=
compare_staged=
merge_base=
left_dir=
right_dir=
diff_tool=
copy_back=

while test $# != 0
do
	case "$1" in
	-h|--h|--he|--hel|--help)
		usage
		;;
	--cached)
		compare_staged=1
		;;
	--copy-back)
		copy_back=1
		;;
	-x|--e|--ex|--ext|--extc|--extcm|--extcmd)
		if test $# = 1
		then
			echo You must specify the tool for use with --extcmd
			usage
		else
			diff_tool=$2
			shift
		fi
		;;
	--)
		dashdash_seen=1
		;;
	-*)
		echo Invalid option: "$1"
		usage
		;;
	*)
		# could be commit, commit range or path limiter
		case "$1" in
		*...*)
			left=${1%...*}
			right=${1#*...}
			merge_base=1
			;;
		*..*)
			left=${1%..*}
			right=${1#*..}
			;;
		*)
			if test -n "$dashdash_seen"
			then
				paths="$paths$1 "
			elif test -z "$left"
			then
				left=$1
			elif test -z "$right"
			then
				right=$1
			else
				paths="$paths$1 "
			fi
			;;
		esac
		;;
	esac
	shift
done

# Determine the set of files which changed
if test -n "$left" && test -n "$right"
then
	left_dir="cmt-$(git rev-parse --short $left)"
	right_dir="cmt-$(git rev-parse --short $right)"

	if test -n "$compare_staged"
	then
		usage
	elif test -n "$merge_base"
	then
		git diff --name-only "$left"..."$right" -- $paths >"$tmp/filelist"
	else
		git diff --name-only "$left" "$right" -- $paths >"$tmp/filelist"
	fi
elif test -n "$left"
then
	left_dir="cmt-$(git rev-parse --short $left)"

	if test -n "$compare_staged"
	then
		right_dir="staged"
		git diff --name-only --cached "$left" -- $paths >"$tmp/filelist"
	else
		right_dir="working_tree"
		git diff --name-only "$left" -- $paths >"$tmp/filelist"
	fi
else
	left_dir="HEAD"

	if test -n "$compare_staged"
	then
		right_dir="staged"
		git diff --name-only --cached -- $paths >"$tmp/filelist"
	else
		right_dir="working_tree"
		git diff --name-only -- $paths >"$tmp/filelist"
	fi
fi

# Exit immediately if there are no diffs
if test ! -s "$tmp/filelist"
then
	exit 0
fi

if test -n "$copy_back" && test "$right_dir" != "working_tree"
then
	echo "--copy-back is only valid when diff includes the working tree."
	exit 1
fi

# Create the named tmp directories that will hold the files to be compared
mkdir -p "$tmp/$left_dir" "$tmp/$right_dir"

# Populate the tmp/right_dir directory with the files to be compared
while read name
do
	if test -n "$right"
	then
		ls_list=$(git ls-tree $right "$name")
		if test -n "$ls_list"
		then
			mkdir -p "$tmp/$right_dir/$(dirname "$name")"
			git show "$right":"$name" >"$tmp/$right_dir/$name" || true
		fi
	elif test -n "$compare_staged"
	then
		ls_list=$(git ls-files -- "$name")
		if test -n "$ls_list"
		then
			mkdir -p "$tmp/$right_dir/$(dirname "$name")"
			git show :"$name" >"$tmp/$right_dir/$name"
		fi
	else
		if test -e "$name"
		then
			mkdir -p "$tmp/$right_dir/$(dirname "$name")"
			cp "$name" "$tmp/$right_dir/$name"
		fi
	fi
done < "$tmp/filelist"

# Populate the tmp/left_dir directory with the files to be compared
while read name
do
	if test -n "$left"
	then
		ls_list=$(git ls-tree $left "$name")
		if test -n "$ls_list"
		then
			mkdir -p "$tmp/$left_dir/$(dirname "$name")"
			git show "$left":"$name" >"$tmp/$left_dir/$name" || true
		fi
	else
		if test -n "$compare_staged"
		then
			ls_list=$(git ls-tree HEAD "$name")
			if test -n "$ls_list"
			then
				mkdir -p "$tmp/$left_dir/$(dirname "$name")"
				git show HEAD:"$name" >"$tmp/$left_dir/$name"
			fi
		else
			mkdir -p "$tmp/$left_dir/$(dirname "$name")"
			git show :"$name" >"$tmp/$left_dir/$name"
		fi
	fi
done < "$tmp/filelist"

LOCAL="$tmp/$left_dir"
REMOTE="$tmp/$right_dir"

if test -n "$diff_tool"
then
	export BASE
	eval $diff_tool '"$LOCAL"' '"$REMOTE"'
else
	run_merge_tool "$merge_tool" false
fi

# Copy files back to the working dir, if requested
if test -n "$copy_back" && test "$right_dir" = "working_tree"
then
	cd "$start_dir"
	git_top_dir=$(git rev-parse --show-toplevel)
	find "$tmp/$right_dir" -type f |
	while read file
	do
		cp "$file" "$git_top_dir/${file#$tmp/$right_dir/}"
	done
fi
