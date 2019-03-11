#!/bin/sh
# Copyright (c) 2007, Nanako Shiraishi

dashless=$(basename "$0" | sed -e 's/-/ /')
USAGE="list [<options>]
   or: $dashless show [<stash>]
   or: $dashless drop [-q|--quiet] [<stash>]
   or: $dashless ( pop | apply ) [--index] [-q|--quiet] [<stash>]
   or: $dashless branch <branchname> [<stash>]
   or: $dashless save [--patch] [-k|--[no-]keep-index] [-q|--quiet]
		      [-u|--include-untracked] [-a|--all] [<message>]
   or: $dashless [push [--patch] [-k|--[no-]keep-index] [-q|--quiet]
		       [-u|--include-untracked] [-a|--all] [-m <message>]
		       [-- <pathspec>...]]
   or: $dashless clear"

SUBDIRECTORY_OK=Yes
OPTIONS_SPEC=
START_DIR=$(pwd)
. git-sh-setup
require_work_tree
prefix=$(git rev-parse --show-prefix) || exit 1
cd_to_toplevel

TMP="$GIT_DIR/.git-stash.$$"
TMPindex=${GIT_INDEX_FILE-"$(git rev-parse --git-path index)"}.stash.$$
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
	git diff-index --quiet --cached HEAD --ignore-submodules -- "$@" &&
	git diff-files --quiet --ignore-submodules -- "$@" &&
	(test -z "$untracked" || test -z "$(untracked_files "$@")")
}

untracked_files () {
	if test "$1" = "-z"
	then
		shift
		z=-z
	else
		z=
	fi
	excl_opt=--exclude-standard
	test "$untracked" = "all" && excl_opt=
	git ls-files -o $z $excl_opt -- "$@"
}

prepare_fallback_ident () {
	if ! git -c user.useconfigonly=yes var GIT_COMMITTER_IDENT >/dev/null 2>&1
	then
		GIT_AUTHOR_NAME="git stash"
		GIT_AUTHOR_EMAIL=git@stash
		GIT_COMMITTER_NAME="git stash"
		GIT_COMMITTER_EMAIL=git@stash
		export GIT_AUTHOR_NAME
		export GIT_AUTHOR_EMAIL
		export GIT_COMMITTER_NAME
		export GIT_COMMITTER_EMAIL
	fi
}

clear_stash () {
	if test $# != 0
	then
		die "$(gettext "git stash clear with parameters is unimplemented")"
	fi
	if current=$(git rev-parse --verify --quiet $ref_stash)
	then
		git update-ref -d $ref_stash $current
	fi
}

create_stash () {

	prepare_fallback_ident

	stash_msg=
	untracked=
	while test $# != 0
	do
		case "$1" in
		-m|--message)
			shift
			stash_msg=${1?"BUG: create_stash () -m requires an argument"}
			;;
		-m*)
			stash_msg=${1#-m}
			;;
		--message=*)
			stash_msg=${1#--message=}
			;;
		-u|--include-untracked)
			shift
			untracked=${1?"BUG: create_stash () -u requires an argument"}
			;;
		--)
			shift
			break
			;;
		esac
		shift
	done

	git update-index -q --refresh
	if no_changes "$@"
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

	if test -n "$untracked"
	then
		# Untracked files are stored by themselves in a parentless commit, for
		# ease of unpacking later.
		u_commit=$(
			untracked_files -z "$@" | (
				GIT_INDEX_FILE="$TMPindex" &&
				export GIT_INDEX_FILE &&
				rm -f "$TMPindex" &&
				git update-index -z --add --remove --stdin &&
				u_tree=$(git write-tree) &&
				printf 'untracked files on %s\n' "$msg" | git commit-tree $u_tree  &&
				rm -f "$TMPindex"
		) ) || die "$(gettext "Cannot save the untracked files")"

		untracked_commit_option="-p $u_commit";
	else
		untracked_commit_option=
	fi

	if test -z "$patch_mode"
	then

		# state of the working tree
		w_tree=$( (
			git read-tree --index-output="$TMPindex" -m $i_tree &&
			GIT_INDEX_FILE="$TMPindex" &&
			export GIT_INDEX_FILE &&
			git diff-index --name-only -z HEAD -- "$@" >"$TMP-stagenames" &&
			git update-index -z --add --remove --stdin <"$TMP-stagenames" &&
			git write-tree &&
			rm -f "$TMPindex"
		) ) ||
			die "$(gettext "Cannot save the current worktree state")"

	else

		rm -f "$TMP-index" &&
		GIT_INDEX_FILE="$TMP-index" git read-tree HEAD &&

		# find out what the user wants
		GIT_INDEX_FILE="$TMP-index" \
			git add--interactive --patch=stash -- "$@" &&

		# state of the working tree
		w_tree=$(GIT_INDEX_FILE="$TMP-index" git write-tree) ||
		die "$(gettext "Cannot save the current worktree state")"

		git diff-tree -p HEAD $w_tree -- >"$TMP-patch" &&
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
	git commit-tree $w_tree -p $b_commit -p $i_commit $untracked_commit_option) ||
	die "$(gettext "Cannot record working tree state")"
}

store_stash () {
	while test $# != 0
	do
		case "$1" in
		-m|--message)
			shift
			stash_msg="$1"
			;;
		-m*)
			stash_msg=${1#-m}
			;;
		--message=*)
			stash_msg=${1#--message=}
			;;
		-q|--quiet)
			quiet=t
			;;
		*)
			break
			;;
		esac
		shift
	done
	test $# = 1 ||
	die "$(eval_gettext "\"$dashless store\" requires one <commit> argument")"

	w_commit="$1"
	if test -z "$stash_msg"
	then
		stash_msg="Created via \"git stash store\"."
	fi

	git update-ref --create-reflog -m "$stash_msg" $ref_stash $w_commit
	ret=$?
	test $ret != 0 && test -z "$quiet" &&
	die "$(eval_gettext "Cannot update \$ref_stash with \$w_commit")"
	return $ret
}

push_stash () {
	keep_index=
	patch_mode=
	untracked=
	stash_msg=
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
		-u|--include-untracked)
			untracked=untracked
			;;
		-a|--all)
			untracked=all
			;;
		-m|--message)
			shift
			test -z ${1+x} && usage
			stash_msg=$1
			;;
		-m*)
			stash_msg=${1#-m}
			;;
		--message=*)
			stash_msg=${1#--message=}
			;;
		--help)
			show_help
			;;
		--)
			shift
			break
			;;
		-*)
			option="$1"
			eval_gettextln "error: unknown option for 'stash push': \$option"
			usage
			;;
		*)
			break
			;;
		esac
		shift
	done

	eval "set $(git rev-parse --sq --prefix "$prefix" -- "$@")"

	if test -n "$patch_mode" && test -n "$untracked"
	then
		die "$(gettext "Can't use --patch and --include-untracked or --all at the same time")"
	fi

	test -n "$untracked" || git ls-files --error-unmatch -- "$@" >/dev/null || exit 1

	git update-index -q --refresh
	if no_changes "$@"
	then
		say "$(gettext "No local changes to save")"
		exit 0
	fi

	git reflog exists $ref_stash ||
		clear_stash || die "$(gettext "Cannot initialize stash")"

	create_stash -m "$stash_msg" -u "$untracked" -- "$@"
	store_stash -m "$stash_msg" -q $w_commit ||
	die "$(gettext "Cannot save the current status")"
	say "$(eval_gettext "Saved working directory and index state \$stash_msg")"

	if test -z "$patch_mode"
	then
		test "$untracked" = "all" && CLEAN_X_OPTION=-x || CLEAN_X_OPTION=
		if test -n "$untracked" && test $# = 0
		then
			git clean --force --quiet -d $CLEAN_X_OPTION
		fi

		if test $# != 0
		then
			test -z "$untracked" && UPDATE_OPTION="-u" || UPDATE_OPTION=
			test "$untracked" = "all" && FORCE_OPTION="--force" || FORCE_OPTION=
			git add $UPDATE_OPTION $FORCE_OPTION -- "$@"
			git diff-index -p --cached --binary HEAD -- "$@" |
			git apply --index -R
		else
			git reset --hard -q
		fi

		if test "$keep_index" = "t" && test -n "$i_tree"
		then
			git read-tree --reset $i_tree
			git ls-files -z --modified -- "$@" |
			git checkout-index -z --force --stdin
		fi
	else
		git apply -R < "$TMP-patch" ||
		die "$(gettext "Cannot remove worktree changes")"

		if test "$keep_index" != "t"
		then
			git reset -q -- "$@"
		fi
	fi
}

save_stash () {
	push_options=
	while test $# != 0
	do
		case "$1" in
		--)
			shift
			break
			;;
		-*)
			# pass all options through to push_stash
			push_options="$push_options $1"
			;;
		*)
			break
			;;
		esac
		shift
	done

	stash_msg="$*"

	if test -z "$stash_msg"
	then
		push_stash $push_options
	else
		push_stash $push_options -m "$stash_msg"
	fi
}

have_stash () {
	git rev-parse --verify --quiet $ref_stash >/dev/null
}

list_stash () {
	have_stash || return 0
	git log --format="%gd: %gs" -g --first-parent -m "$@" $ref_stash --
}

show_stash () {
	ALLOW_UNKNOWN_FLAGS=t
	assert_stash_like "$@"

	if test -z "$FLAGS"
	then
		if test "$(git config --bool stash.showStat || echo true)" = "true"
		then
			FLAGS=--stat
		fi

		if test "$(git config --bool stash.showPatch || echo false)" = "true"
		then
			FLAGS=${FLAGS}${FLAGS:+ }-p
		fi

		if test -z "$FLAGS"
		then
			return 0
		fi
	fi

	git diff ${FLAGS} $b_commit $w_commit
}

show_help () {
	exec git help stash
	exit 1
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
#   u_commit is set to the commit containing the untracked files tree
#   w_tree is set to the working tree
#   b_tree is set to the base tree
#   i_tree is set to the index tree
#   u_tree is set to the untracked files tree
#
#   GIT_QUIET is set to t if -q is specified
#   INDEX_OPTION is set to --index if --index is specified.
#   FLAGS is set to the remaining flags (if allowed)
#
# dies if:
#   * too many revisions specified
#   * no revision is specified and there is no stash stack
#   * a revision is specified which cannot be resolve to a SHA1
#   * a non-existent stash reference is specified
#   * unknown flags were set and ALLOW_UNKNOWN_FLAGS is not "t"
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
	u_commit=
	w_tree=
	b_tree=
	i_tree=
	u_tree=

	FLAGS=
	REV=
	for opt
	do
		case "$opt" in
			-q|--quiet)
				GIT_QUIET=-t
			;;
			--index)
				INDEX_OPTION=--index
			;;
			--help)
				show_help
			;;
			-*)
				test "$ALLOW_UNKNOWN_FLAGS" = t ||
					die "$(eval_gettext "unknown option: \$opt")"
				FLAGS="${FLAGS}${FLAGS:+ }$opt"
			;;
			*)
				REV="${REV}${REV:+ }'$opt'"
			;;
		esac
	done

	eval set -- $REV

	case $# in
		0)
			have_stash || die "$(gettext "No stash entries found.")"
			set -- ${ref_stash}@{0}
		;;
		1)
			:
		;;
		*)
			die "$(eval_gettext "Too many revisions specified: \$REV")"
		;;
	esac

	case "$1" in
		*[!0-9]*)
			:
		;;
		*)
			set -- "${ref_stash}@{$1}"
		;;
	esac

	REV=$(git rev-parse --symbolic --verify --quiet "$1") || {
		reference="$1"
		die "$(eval_gettext "\$reference is not a valid reference")"
	}

	i_commit=$(git rev-parse --verify --quiet "$REV^2") &&
	set -- $(git rev-parse "$REV" "$REV^1" "$REV:" "$REV^1:" "$REV^2:" 2>/dev/null) &&
	s=$1 &&
	w_commit=$1 &&
	b_commit=$2 &&
	w_tree=$3 &&
	b_tree=$4 &&
	i_tree=$5 &&
	IS_STASH_LIKE=t &&
	test "$ref_stash" = "$(git rev-parse --symbolic-full-name "${REV%@*}")" &&
	IS_STASH_REF=t

	u_commit=$(git rev-parse --verify --quiet "$REV^3") &&
	u_tree=$(git rev-parse "$REV^3:" 2>/dev/null)
}

is_stash_like()
{
	parse_flags_and_rev "$@"
	test -n "$IS_STASH_LIKE"
}

assert_stash_like() {
	is_stash_like "$@" || {
		args="$*"
		die "$(eval_gettext "'\$args' is not a stash-like commit")"
	}
}

is_stash_ref() {
	is_stash_like "$@" && test -n "$IS_STASH_REF"
}

assert_stash_ref() {
	is_stash_ref "$@" || {
		args="$*"
		die "$(eval_gettext "'\$args' is not a stash reference")"
	}
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

	if test -n "$u_tree"
	then
		GIT_INDEX_FILE="$TMPindex" git read-tree "$u_tree" &&
		GIT_INDEX_FILE="$TMPindex" git checkout-index --all &&
		rm -f "$TMPindex" ||
		die "$(gettext "Could not restore untracked files from stash entry")"
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
		git rerere
		if test -n "$INDEX_OPTION"
		then
			gettextln "Index was not unstashed." >&2
		fi
		exit $status
	fi
}

pop_stash() {
	assert_stash_ref "$@"

	if apply_stash "$@"
	then
		drop_stash "$@"
	else
		status=$?
		say "$(gettext "The stash entry is kept in case you need it again.")"
		exit $status
	fi
}

drop_stash () {
	assert_stash_ref "$@"

	git reflog delete --updateref --rewrite "${REV}" &&
		say "$(eval_gettext "Dropped \${REV} (\$s)")" ||
		die "$(eval_gettext "\${REV}: Could not drop stash entry")"

	# clear_stash if we just dropped the last stash entry
	git rev-parse --verify --quiet "$ref_stash@{0}" >/dev/null ||
	clear_stash
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

test "$1" = "-p" && set "push" "$@"

PARSE_CACHE='--not-parsed'
# The default command is "push" if nothing but options are given
seen_non_option=
for opt
do
	case "$opt" in
	--) break ;;
	-*) ;;
	*) seen_non_option=t; break ;;
	esac
done

test -n "$seen_non_option" || set "push" "$@"

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
push)
	shift
	push_stash "$@"
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
	shift
	create_stash -m "$*" && echo "$w_commit"
	;;
store)
	shift
	store_stash "$@"
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
		push_stash &&
		say "$(gettext "(To restore them type \"git stash apply\")")"
		;;
	*)
		usage
	esac
	;;
esac
