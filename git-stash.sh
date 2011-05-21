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
START_DIR=`pwd`
. git-sh-setup
. git-sh-i18n
require_work_tree
cd_to_toplevel

TMP="$GIT_DIR/.git-stash.$$"
TMPindex=${GIT_INDEX_FILE-"$GIT_DIR/index"}.stash.$$
trap 'rm -f "$TMP-"* "$TMPindex"' 0

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
		die "$(gettext "git stash clear with parameters is unimplemented")"
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
		die "$(gettext "You do not have the initial commit yet")"
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
		die "$(gettext "Cannot save the current index state")"

	if test -z "$patch_mode"
	then

		# state of the working tree
		w_tree=$( (
			git read-tree --index-output="$TMPindex" -m $i_tree &&
			GIT_INDEX_FILE="$TMPindex" &&
			export GIT_INDEX_FILE &&
			git diff --name-only -z HEAD | git update-index -z --add --remove --stdin &&
			git write-tree &&
			rm -f "$TMPindex"
		) ) ||
			die "$(gettext "Cannot save the current worktree state")"

	else

		rm -f "$TMP-index" &&
		GIT_INDEX_FILE="$TMP-index" git read-tree HEAD &&

		# find out what the user wants
		GIT_INDEX_FILE="$TMP-index" \
			git add--interactive --patch=stash -- &&

		# state of the working tree
		w_tree=$(GIT_INDEX_FILE="$TMP-index" git write-tree) ||
		die "$(gettext "Cannot save the current worktree state")"

		git diff-tree -p HEAD $w_tree > "$TMP-patch" &&
		test -s "$TMP-patch" ||
		die "$(gettext "No changes selected")"

		rm -f "$TMP-index" ||
		die "$(gettext "Cannot remove temporary index (can't happen)")"

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
		die "$(gettext "Cannot record working tree state")"
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
			keep_index=n
			;;
		-p|--patch)
			patch_mode=t
			# only default to keep if we don't already have an override
			test -z "$keep_index" && keep_index=t
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
		say "$(gettext "No local changes to save")"
		exit 0
	fi
	test -f "$GIT_DIR/logs/$ref_stash" ||
		clear_stash || die "$(gettext "Cannot initialize stash")"

	create_stash "$stash_msg"

	# Make sure the reflog for stash is kept.
	: >>"$GIT_DIR/logs/$ref_stash"

	git update-ref -m "$stash_msg" $ref_stash $w_commit ||
		die "$(gettext "Cannot save the current status")"
	say Saved working directory and index state "$stash_msg"

	if test -z "$patch_mode"
	then
		git reset --hard ${GIT_QUIET:+-q}

		if test "$keep_index" = "t" && test -n $i_tree
		then
			git read-tree --reset -u $i_tree
		fi
	else
		git apply -R < "$TMP-patch" ||
		die "$(gettext "Cannot remove worktree changes")"

		if test "$keep_index" != "t"
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
	assert_stash_like "$@"

	git diff ${FLAGS:---stat} $b_commit $w_commit
}

#
# Parses the remaining options looking for flags and
# at most one revision defaulting to ${ref_stash}@{0}
# if none found.
#
# Derives related tree and commit objects from the
# revision, if one is found.
#
# stash records the work tree, and is a merge between the
# base commit (first parent) and the index tree (second parent).
#
#   REV is set to the symbolic version of the specified stash-like commit
#   IS_STASH_LIKE is non-blank if ${REV} looks like a stash
#   IS_STASH_REF is non-blank if the ${REV} looks like a stash ref
#   s is set to the SHA1 of the stash commit
#   w_commit is set to the commit containing the working tree
#   b_commit is set to the base commit
#   i_commit is set to the commit containing the index tree
#   w_tree is set to the working tree
#   b_tree is set to the base tree
#   i_tree is set to the index tree
#
#   GIT_QUIET is set to t if -q is specified
#   INDEX_OPTION is set to --index if --index is specified.
#   FLAGS is set to the remaining flags
#
# dies if:
#   * too many revisions specified
#   * no revision is specified and there is no stash stack
#   * a revision is specified which cannot be resolve to a SHA1
#   * a non-existent stash reference is specified
#

parse_flags_and_rev()
{
	test "$PARSE_CACHE" = "$*" && return 0 # optimisation
	PARSE_CACHE="$*"

	IS_STASH_LIKE=
	IS_STASH_REF=
	INDEX_OPTION=
	s=
	w_commit=
	b_commit=
	i_commit=
	w_tree=
	b_tree=
	i_tree=

	REV=$(git rev-parse --no-flags --symbolic "$@") || exit 1

	FLAGS=
	for opt
	do
		case "$opt" in
			-q|--quiet)
				GIT_QUIET=-t
			;;
			--index)
				INDEX_OPTION=--index
			;;
			-*)
				FLAGS="${FLAGS}${FLAGS:+ }$opt"
			;;
		esac
	done

	set -- $REV

	case $# in
		0)
			have_stash || die "$(gettext "No stash found.")"
			set -- ${ref_stash}@{0}
		;;
		1)
			:
		;;
		*)
			die "Too many revisions specified: $REV"
		;;
	esac

	REV=$(git rev-parse --quiet --symbolic --verify $1 2>/dev/null) || die "$1 is not valid reference"

	i_commit=$(git rev-parse --quiet --verify $REV^2 2>/dev/null) &&
	set -- $(git rev-parse $REV $REV^1 $REV: $REV^1: $REV^2: 2>/dev/null) &&
	s=$1 &&
	w_commit=$1 &&
	b_commit=$2 &&
	w_tree=$3 &&
	b_tree=$4 &&
	i_tree=$5 &&
	IS_STASH_LIKE=t &&
	test "$ref_stash" = "$(git rev-parse --symbolic-full-name "${REV%@*}")" &&
	IS_STASH_REF=t
}

is_stash_like()
{
	parse_flags_and_rev "$@"
	test -n "$IS_STASH_LIKE"
}

assert_stash_like() {
	is_stash_like "$@" || die "'$*' is not a stash-like commit"
}

is_stash_ref() {
	is_stash_like "$@" && test -n "$IS_STASH_REF"
}

assert_stash_ref() {
	is_stash_ref "$@" || die "'$*' is not a stash reference"
}

apply_stash () {

	assert_stash_like "$@"

	git update-index -q --refresh || die "$(gettext "unable to refresh index")"

	# current index state
	c_tree=$(git write-tree) ||
		die "$(gettext "Cannot apply a stash in the middle of a merge")"

	unstashed_index_tree=
	if test -n "$INDEX_OPTION" && test "$b_tree" != "$i_tree" &&
			test "$c_tree" != "$i_tree"
	then
		git diff-tree --binary $s^2^..$s^2 | git apply --cached
		test $? -ne 0 &&
			die "$(gettext "Conflicts in index. Try without --index.")"
		unstashed_index_tree=$(git write-tree) ||
			die "$(gettext "Could not save index tree")"
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
		GIT_MERGE_VERBOSITY=0 && export GIT_MERGE_VERBOSITY
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
				die "$(gettext "Cannot unstage modified files")"
			rm -f "$a"
		fi
		squelch=
		if test -n "$GIT_QUIET"
		then
			squelch='>/dev/null 2>&1'
		fi
		(cd "$START_DIR" && eval "git status $squelch") || :
	else
		# Merge conflict; keep the exit status from merge-recursive
		status=$?
		if test -n "$INDEX_OPTION"
		then
			(
				gettext "Index was not unstashed." &&
				echo
			) >&2
		fi
		exit $status
	fi
}

pop_stash() {
	assert_stash_ref "$@"

	apply_stash "$@" &&
	drop_stash "$@"
}

drop_stash () {
	assert_stash_ref "$@"

	git reflog delete --updateref --rewrite "${REV}" &&
		say "Dropped ${REV} ($s)" || die "${REV}: Could not drop stash entry"

	# clear_stash if we just dropped the last stash entry
	git rev-parse --verify "$ref_stash@{0}" > /dev/null 2>&1 || clear_stash
}

apply_to_branch () {
	test -n "$1" || die "$(gettext "No branch name specified")"
	branch=$1
	shift 1

	set -- --index "$@"
	assert_stash_like "$@"

	git checkout -b $branch $REV^ &&
	apply_stash "$@" && {
		test -z "$IS_STASH_REF" || drop_stash "$@"
	}
}

PARSE_CACHE='--not-parsed'
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
	pop_stash "$@"
	;;
branch)
	shift
	apply_to_branch "$@"
	;;
*)
	case $# in
	0)
		save_stash &&
		say "$(gettext "(To restore them type \"git stash apply\")")"
		;;
	*)
		usage
	esac
	;;
esac
