#!/bin/sh
# Copyright (c) 2007, Nanako Shiraishi

dashless=$(basename "$0" | sed -e 's/-/ /')
USAGE="list [<options>]
   or: $dashless show [<stash>]
   or: $dashless drop [-q|--quiet] [<stash>]
   or: $dashless ( pop | apply ) [--index] [-q|--quiet] [<stash>]
   or: $dashless branch <branchname> [<stash>]
   or: $dashless [save [--patch] [-k|--[no-]keep-index] [-q|--quiet] [<message>]]
   or: $dashless clear"

SUBDIRECTORY_OK=Yes
OPTIONS_SPEC=
. git-sh-setup
require_work_tree
cd_to_toplevel

TMP="$GIT_DIR/.git-stash.$$"
trap 'rm -f "$TMP-*"' 0

ref_stash=refs/stash

if git config --get-colorbool color.interactive; then
       help_color="$(git config --get-color color.interactive.help 'red bold')"
       reset_color="$(git config --get-color '' reset)"
else
       help_color=
       reset_color=
fi

no_changes () {
	git diff-index --quiet --cached HEAD --ignore-submodules -- &&
	git diff-files --quiet --ignore-submodules
}

clear_stash () {
	if test $# != 0
	then
		die "git stash clear with parameters is unimplemented"
	fi
	if current=$(git rev-parse --verify $ref_stash 2>/dev/null)
	then
		git update-ref -d $ref_stash $current
	fi
}

create_stash () {
	stash_msg="$1"

	git update-index -q --refresh
	if no_changes
	then
		exit 0
	fi

	# state of the base commit
	if b_commit=$(git rev-parse --verify HEAD)
	then
		head=$(git rev-list --oneline -n 1 HEAD --)
	else
		die "You do not have the initial commit yet"
	fi

	if branch=$(git symbolic-ref -q HEAD)
	then
		branch=${branch#refs/heads/}
	else
		branch='(no branch)'
	fi
	msg=$(printf '%s: %s' "$branch" "$head")

	# state of the index
	i_tree=$(git write-tree) &&
	i_commit=$(printf 'index on %s\n' "$msg" |
		git commit-tree $i_tree -p $b_commit) ||
		die "Cannot save the current index state"

	if test -z "$patch_mode"
	then

		# state of the working tree
		w_tree=$( (
			rm -f "$TMP-index" &&
			cp -p ${GIT_INDEX_FILE-"$GIT_DIR/index"} "$TMP-index" &&
			GIT_INDEX_FILE="$TMP-index" &&
			export GIT_INDEX_FILE &&
			git read-tree -m $i_tree &&
			git diff --name-only -z HEAD | git update-index -z --add --remove --stdin &&
			git write-tree &&
			rm -f "$TMP-index"
		) ) ||
			die "Cannot save the current worktree state"

	else

		rm -f "$TMP-index" &&
		GIT_INDEX_FILE="$TMP-index" git read-tree HEAD &&

		# find out what the user wants
		GIT_INDEX_FILE="$TMP-index" \
			git add--interactive --patch=stash -- &&

		# state of the working tree
		w_tree=$(GIT_INDEX_FILE="$TMP-index" git write-tree) ||
		die "Cannot save the current worktree state"

		git diff-tree -p HEAD $w_tree > "$TMP-patch" &&
		test -s "$TMP-patch" ||
		die "No changes selected"

		rm -f "$TMP-index" ||
		die "Cannot remove temporary index (can't happen)"

	fi

	# create the stash
	if test -z "$stash_msg"
	then
		stash_msg=$(printf 'WIP on %s' "$msg")
	else
		stash_msg=$(printf 'On %s: %s' "$branch" "$stash_msg")
	fi
	w_commit=$(printf '%s\n' "$stash_msg" |
		git commit-tree $w_tree -p $b_commit -p $i_commit) ||
		die "Cannot record working tree state"
}

save_stash () {
	keep_index=
	patch_mode=
	while test $# != 0
	do
		case "$1" in
		-k|--keep-index)
			keep_index=t
			;;
		--no-keep-index)
			keep_index=
			;;
		-p|--patch)
			patch_mode=t
			keep_index=t
			;;
		-q|--quiet)
			GIT_QUIET=t
			;;
		--)
			shift
			break
			;;
		-*)
			echo "error: unknown option for 'stash save': $1"
			echo "       To provide a message, use git stash save -- '$1'"
			usage
			;;
		*)
			break
			;;
		esac
		shift
	done

	stash_msg="$*"

	git update-index -q --refresh
	if no_changes
	then
		say 'No local changes to save'
		exit 0
	fi
	test -f "$GIT_DIR/logs/$ref_stash" ||
		clear_stash || die "Cannot initialize stash"

	create_stash "$stash_msg"

	# Make sure the reflog for stash is kept.
	: >>"$GIT_DIR/logs/$ref_stash"

	git update-ref -m "$stash_msg" $ref_stash $w_commit ||
		die "Cannot save the current status"
	say Saved working directory and index state "$stash_msg"

	if test -z "$patch_mode"
	then
		git reset --hard ${GIT_QUIET:+-q}

		if test -n "$keep_index" && test -n $i_tree
		then
			git read-tree --reset -u $i_tree
		fi
	else
		git apply -R < "$TMP-patch" ||
		die "Cannot remove worktree changes"

		if test -z "$keep_index"
		then
			git reset
		fi
	fi
}

have_stash () {
	git rev-parse --verify $ref_stash >/dev/null 2>&1
}

list_stash () {
	have_stash || return 0
	git log --format="%gd: %gs" -g "$@" $ref_stash --
}

show_stash () {
	have_stash || die 'No stash found'

	flags=$(git rev-parse --no-revs --flags "$@")
	if test -z "$flags"
	then
		flags=--stat
	fi

	w_commit=$(git rev-parse --quiet --verify --default $ref_stash "$@") &&
	b_commit=$(git rev-parse --quiet --verify "$w_commit^") ||
		die "'$*' is not a stash"

	git diff $flags $b_commit $w_commit
}

apply_stash () {
	applied_stash=
	unstash_index=

	while test $# != 0
	do
		case "$1" in
		--index)
			unstash_index=t
			;;
		-q|--quiet)
			GIT_QUIET=t
			;;
		*)
			break
			;;
		esac
		shift
	done

	if test $# = 0
	then
		have_stash || die 'Nothing to apply'
		applied_stash="$ref_stash@{0}"
	else
		applied_stash="$*"
	fi

	# stash records the work tree, and is a merge between the
	# base commit (first parent) and the index tree (second parent).
	s=$(git rev-parse --quiet --verify --default $ref_stash "$@") &&
	w_tree=$(git rev-parse --quiet --verify "$s:") &&
	b_tree=$(git rev-parse --quiet --verify "$s^1:") &&
	i_tree=$(git rev-parse --quiet --verify "$s^2:") ||
		die "$*: no valid stashed state found"

	git update-index -q --refresh &&
	git diff-files --quiet --ignore-submodules ||
		die 'Cannot apply to a dirty working tree, please stage your changes'

	# current index state
	c_tree=$(git write-tree) ||
		die 'Cannot apply a stash in the middle of a merge'

	unstashed_index_tree=
	if test -n "$unstash_index" && test "$b_tree" != "$i_tree" &&
			test "$c_tree" != "$i_tree"
	then
		git diff-tree --binary $s^2^..$s^2 | git apply --cached
		test $? -ne 0 &&
			die 'Conflicts in index. Try without --index.'
		unstashed_index_tree=$(git write-tree) ||
			die 'Could not save index tree'
		git reset
	fi

	eval "
		GITHEAD_$w_tree='Stashed changes' &&
		GITHEAD_$c_tree='Updated upstream' &&
		GITHEAD_$b_tree='Version stash was based on' &&
		export GITHEAD_$w_tree GITHEAD_$c_tree GITHEAD_$b_tree
	"

	if test -n "$GIT_QUIET"
	then
		export GIT_MERGE_VERBOSITY=0
	fi
	if git merge-recursive $b_tree -- $c_tree $w_tree
	then
		# No conflict
		if test -n "$unstashed_index_tree"
		then
			git read-tree "$unstashed_index_tree"
		else
			a="$TMP-added" &&
			git diff-index --cached --name-only --diff-filter=A $c_tree >"$a" &&
			git read-tree --reset $c_tree &&
			git update-index --add --stdin <"$a" ||
				die "Cannot unstage modified files"
			rm -f "$a"
		fi
		squelch=
		if test -n "$GIT_QUIET"
		then
			squelch='>/dev/null 2>&1'
		fi
		eval "git status $squelch" || :
	else
		# Merge conflict; keep the exit status from merge-recursive
		status=$?
		if test -n "$unstash_index"
		then
			echo >&2 'Index was not unstashed.'
		fi
		exit $status
	fi
}

drop_stash () {
	have_stash || die 'No stash entries to drop'

	while test $# != 0
	do
		case "$1" in
		-q|--quiet)
			GIT_QUIET=t
			;;
		*)
			break
			;;
		esac
		shift
	done

	if test $# = 0
	then
		set x "$ref_stash@{0}"
		shift
	fi
	# Verify supplied argument looks like a stash entry
	s=$(git rev-parse --verify "$@") &&
	git rev-parse --verify "$s:"   > /dev/null 2>&1 &&
	git rev-parse --verify "$s^1:" > /dev/null 2>&1 &&
	git rev-parse --verify "$s^2:" > /dev/null 2>&1 ||
		die "$*: not a valid stashed state"

	git reflog delete --updateref --rewrite "$@" &&
		say "Dropped $* ($s)" || die "$*: Could not drop stash entry"

	# clear_stash if we just dropped the last stash entry
	git rev-parse --verify "$ref_stash@{0}" > /dev/null 2>&1 || clear_stash
}

apply_to_branch () {
	have_stash || die 'Nothing to apply'

	test -n "$1" || die 'No branch name specified'
	branch=$1

	if test -z "$2"
	then
		set x "$ref_stash@{0}"
	fi
	stash=$2

	git checkout -b $branch $stash^ &&
	apply_stash --index $stash &&
	drop_stash $stash
}

# The default command is "save" if nothing but options are given
seen_non_option=
for opt
do
	case "$opt" in
	-*) ;;
	*) seen_non_option=t; break ;;
	esac
done

test -n "$seen_non_option" || set "save" "$@"

# Main command set
case "$1" in
list)
	shift
	list_stash "$@"
	;;
show)
	shift
	show_stash "$@"
	;;
save)
	shift
	save_stash "$@"
	;;
apply)
	shift
	apply_stash "$@"
	;;
clear)
	shift
	clear_stash "$@"
	;;
create)
	if test $# -gt 0 && test "$1" = create
	then
		shift
	fi
	create_stash "$*" && echo "$w_commit"
	;;
drop)
	shift
	drop_stash "$@"
	;;
pop)
	shift
	if apply_stash "$@"
	then
		drop_stash "$applied_stash"
	fi
	;;
branch)
	shift
	apply_to_branch "$@"
	;;
*)
	case $# in
	0)
		save_stash &&
		say '(To restore them type "git stash apply")'
		;;
	*)
		usage
	esac
	;;
esac
