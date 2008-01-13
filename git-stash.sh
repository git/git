#!/bin/sh
# Copyright (c) 2007, Nanako Shiraishi

USAGE='[  | save | list | show | apply | clear | create ]'

SUBDIRECTORY_OK=Yes
OPTIONS_SPEC=
. git-sh-setup
require_work_tree
cd_to_toplevel

TMP="$GIT_DIR/.git-stash.$$"
trap 'rm -f "$TMP-*"' 0

ref_stash=refs/stash

no_changes () {
	git diff-index --quiet --cached HEAD -- &&
	git diff-files --quiet
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

	if no_changes
	then
		exit 0
	fi

	# state of the base commit
	if b_commit=$(git rev-parse --verify HEAD)
	then
		head=$(git log --no-color --abbrev-commit --pretty=oneline -n 1 HEAD --)
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

	# state of the working tree
	w_tree=$( (
		rm -f "$TMP-index" &&
		cp -p ${GIT_INDEX_FILE-"$GIT_DIR/index"} "$TMP-index" &&
		GIT_INDEX_FILE="$TMP-index" &&
		export GIT_INDEX_FILE &&
		git read-tree -m $i_tree &&
		git add -u &&
		git write-tree &&
		rm -f "$TMP-index"
	) ) ||
		die "Cannot save the current worktree state"

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
	stash_msg="$1"

	if no_changes
	then
		echo 'No local changes to save'
		exit 0
	fi
	test -f "$GIT_DIR/logs/$ref_stash" ||
		clear_stash || die "Cannot initialize stash"

	create_stash "$stash_msg"

	# Make sure the reflog for stash is kept.
	: >>"$GIT_DIR/logs/$ref_stash"

	git update-ref -m "$stash_msg" $ref_stash $w_commit ||
		die "Cannot save the current status"
	printf 'Saved working directory and index state "%s"\n' "$stash_msg"
}

have_stash () {
	git rev-parse --verify $ref_stash >/dev/null 2>&1
}

list_stash () {
	have_stash || return 0
	git log --no-color --pretty=oneline -g "$@" $ref_stash -- |
	sed -n -e 's/^[.0-9a-f]* refs\///p'
}

show_stash () {
	flags=$(git rev-parse --no-revs --flags "$@")
	if test -z "$flags"
	then
		flags=--stat
	fi
	s=$(git rev-parse --revs-only --no-flags --default $ref_stash "$@")

	w_commit=$(git rev-parse --verify "$s") &&
	b_commit=$(git rev-parse --verify "$s^") &&
	git diff $flags $b_commit $w_commit
}

apply_stash () {
	git diff-files --quiet ||
		die 'Cannot restore on top of a dirty state'

	unstash_index=
	case "$1" in
	--index)
		unstash_index=t
		shift
	esac

	# current index state
	c_tree=$(git write-tree) ||
		die 'Cannot apply a stash in the middle of a merge'

	# stash records the work tree, and is a merge between the
	# base commit (first parent) and the index tree (second parent).
	s=$(git rev-parse --revs-only --no-flags --default $ref_stash "$@") &&
	w_tree=$(git rev-parse --verify "$s:") &&
	b_tree=$(git rev-parse --verify "$s^1:") &&
	i_tree=$(git rev-parse --verify "$s^2:") ||
		die "$*: no valid stashed state found"

	unstashed_index_tree=
	if test -n "$unstash_index" && test "$b_tree" != "$i_tree"
	then
		git diff-tree --binary $s^2^..$s^2 | git apply --cached
		test $? -ne 0 &&
			die 'Conflicts in index. Try without --index.'
		unstashed_index_tree=$(git-write-tree) ||
			die 'Could not save index tree'
		git reset
	fi

	eval "
		GITHEAD_$w_tree='Stashed changes' &&
		GITHEAD_$c_tree='Updated upstream' &&
		GITHEAD_$b_tree='Version stash was based on' &&
		export GITHEAD_$w_tree GITHEAD_$c_tree GITHEAD_$b_tree
	"

	if git-merge-recursive $b_tree -- $c_tree $w_tree
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
		git status || :
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

# Main command set
case "$1" in
list)
	shift
	if test $# = 0
	then
		set x -n 10
		shift
	fi
	list_stash "$@"
	;;
show)
	shift
	show_stash "$@"
	;;
save)
	shift
	save_stash "$*" && git-reset --hard
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
*)
	if test $# -eq 0
	then
		save_stash &&
		echo '(To restore them type "git stash apply")' &&
		git-reset --hard
	else
		usage
	fi
	;;
esac
