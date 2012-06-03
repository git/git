#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

USAGE='[--interactive | -i] [-v] [--force-rebase | -f] [--no-ff] [--onto <newbase>] [<upstream>|--root] [<branch>] [--quiet | -q]'
LONG_USAGE='git-rebase replaces <branch> with a new branch of the
same name.  When the --onto option is provided the new branch starts
out with a HEAD equal to <newbase>, otherwise it is equal to <upstream>
It then attempts to create a new commit for each commit from the original
<branch> that does not exist in the <upstream> branch.

It is possible that a merge failure will prevent this process from being
completely automatic.  You will have to resolve any such merge failure
and run git rebase --continue.  Another option is to bypass the commit
that caused the merge failure with git rebase --skip.  To check out the
original <branch> and remove the .git/rebase-apply working files, use the
command git rebase --abort instead.

Note that if <branch> is not specified on the command line, the
currently checked out branch is used.

Example:       git-rebase master~1 topic

	A---B---C topic                   A'\''--B'\''--C'\'' topic
       /                   -->           /
  D---E---F---G master          D---E---F---G master
'

SUBDIRECTORY_OK=Yes
OPTIONS_KEEPDASHDASH=
OPTIONS_SPEC="\
git rebase [-i] [options] [--onto <newbase>] [<upstream>] [<branch>]
git rebase [-i] [options] --onto <newbase> --root [<branch>]
git-rebase [-i] --continue | --abort | --skip
--
 Available options are
v,verbose!         display a diffstat of what changed upstream
q,quiet!           be quiet. implies --no-stat
onto=!             rebase onto given branch instead of upstream
p,preserve-merges! try to recreate merges instead of ignoring them
s,strategy=!       use the given merge strategy
no-ff!             cherry-pick all commits, even if unchanged
m,merge!           use merging strategies to rebase
i,interactive!     let the user edit the list of commits to rebase
f,force-rebase!    force rebase even if branch is up to date
X,strategy-option=! pass the argument through to the merge strategy
stat!              display a diffstat of what changed upstream
n,no-stat!         do not show diffstat of what changed upstream
verify             allow pre-rebase hook to run
rerere-autoupdate  allow rerere to update index with resolved conflicts
root!              rebase all reachable commits up to the root(s)
autosquash         move commits that begin with squash!/fixup! under -i
committer-date-is-author-date! passed to 'git am'
ignore-date!       passed to 'git am'
whitespace=!       passed to 'git apply'
ignore-whitespace! passed to 'git apply'
C=!                passed to 'git apply'
 Actions:
continue!          continue
abort!             abort and check out the original branch
skip!              skip current patch and continue
"
. git-sh-setup
set_reflog_action rebase
require_work_tree_exists
cd_to_toplevel

LF='
'
ok_to_skip_pre_rebase=
resolvemsg="
When you have resolved this problem run \"git rebase --continue\".
If you would prefer to skip this patch, instead run \"git rebase --skip\".
To check out the original branch and stop rebasing run \"git rebase --abort\".
"
unset onto
strategy=
strategy_opts=
do_merge=
merge_dir="$GIT_DIR"/rebase-merge
apply_dir="$GIT_DIR"/rebase-apply
verbose=
diffstat=
test "$(git config --bool rebase.stat)" = true && diffstat=t
git_am_opt=
rebase_root=
force_rebase=
allow_rerere_autoupdate=
# Non-empty if a rebase was in progress when 'git rebase' was invoked
in_progress=
# One of {am, merge, interactive}
type=
# One of {"$GIT_DIR"/rebase-apply, "$GIT_DIR"/rebase-merge}
state_dir=
# One of {'', continue, skip, abort}, as parsed from command line
action=
preserve_merges=
autosquash=
test "$(git config --bool rebase.autosquash)" = "true" && autosquash=t

read_basic_state () {
	head_name=$(cat "$state_dir"/head-name) &&
	onto=$(cat "$state_dir"/onto) &&
	# We always write to orig-head, but interactive rebase used to write to
	# head. Fall back to reading from head to cover for the case that the
	# user upgraded git with an ongoing interactive rebase.
	if test -f "$state_dir"/orig-head
	then
		orig_head=$(cat "$state_dir"/orig-head)
	else
		orig_head=$(cat "$state_dir"/head)
	fi &&
	GIT_QUIET=$(cat "$state_dir"/quiet) &&
	test -f "$state_dir"/verbose && verbose=t
	test -f "$state_dir"/strategy && strategy="$(cat "$state_dir"/strategy)"
	test -f "$state_dir"/strategy_opts &&
		strategy_opts="$(cat "$state_dir"/strategy_opts)"
	test -f "$state_dir"/allow_rerere_autoupdate &&
		allow_rerere_autoupdate="$(cat "$state_dir"/allow_rerere_autoupdate)"
}

write_basic_state () {
	echo "$head_name" > "$state_dir"/head-name &&
	echo "$onto" > "$state_dir"/onto &&
	echo "$orig_head" > "$state_dir"/orig-head &&
	echo "$GIT_QUIET" > "$state_dir"/quiet &&
	test t = "$verbose" && : > "$state_dir"/verbose
	test -n "$strategy" && echo "$strategy" > "$state_dir"/strategy
	test -n "$strategy_opts" && echo "$strategy_opts" > \
		"$state_dir"/strategy_opts
	test -n "$allow_rerere_autoupdate" && echo "$allow_rerere_autoupdate" > \
		"$state_dir"/allow_rerere_autoupdate
}

output () {
	case "$verbose" in
	'')
		output=$("$@" 2>&1 )
		status=$?
		test $status != 0 && printf "%s\n" "$output"
		return $status
		;;
	*)
		"$@"
		;;
	esac
}

move_to_original_branch () {
	case "$head_name" in
	refs/*)
		message="rebase finished: $head_name onto $onto"
		git update-ref -m "$message" \
			$head_name $(git rev-parse HEAD) $orig_head &&
		git symbolic-ref \
			-m "rebase finished: returning to $head_name" \
			HEAD $head_name ||
		die "Could not move back to $head_name"
		;;
	esac
}

run_specific_rebase () {
	if [ "$interactive_rebase" = implied ]; then
		GIT_EDITOR=:
		export GIT_EDITOR
	fi
	. git-rebase--$type
}

run_pre_rebase_hook () {
	if test -z "$ok_to_skip_pre_rebase" &&
	   test -x "$GIT_DIR/hooks/pre-rebase"
	then
		"$GIT_DIR/hooks/pre-rebase" ${1+"$@"} ||
		die "The pre-rebase hook refused to rebase."
	fi
}

test -f "$apply_dir"/applying &&
	die 'It looks like git-am is in progress. Cannot rebase.'

if test -d "$apply_dir"
then
	type=am
	state_dir="$apply_dir"
elif test -d "$merge_dir"
then
	if test -f "$merge_dir"/interactive
	then
		type=interactive
		interactive_rebase=explicit
	else
		type=merge
	fi
	state_dir="$merge_dir"
fi
test -n "$type" && in_progress=t

total_argc=$#
while test $# != 0
do
	case "$1" in
	--no-verify)
		ok_to_skip_pre_rebase=yes
		;;
	--verify)
		ok_to_skip_pre_rebase=
		;;
	--continue|--skip|--abort)
		test $total_argc -eq 2 || usage
		action=${1##--}
		;;
	--onto)
		test 2 -le "$#" || usage
		onto="$2"
		shift
		;;
	-i)
		interactive_rebase=explicit
		;;
	-p)
		preserve_merges=t
		test -z "$interactive_rebase" && interactive_rebase=implied
		;;
	--autosquash)
		autosquash=t
		;;
	--no-autosquash)
		autosquash=
		;;
	-M|-m)
		do_merge=t
		;;
	-X)
		shift
		strategy_opts="$strategy_opts $(git rev-parse --sq-quote "--$1")"
		do_merge=t
		test -z "$strategy" && strategy=recursive
		;;
	-s)
		shift
		strategy="$1"
		do_merge=t
		;;
	-n)
		diffstat=
		;;
	--stat)
		diffstat=t
		;;
	-v)
		verbose=t
		diffstat=t
		GIT_QUIET=
		;;
	-q)
		GIT_QUIET=t
		git_am_opt="$git_am_opt -q"
		verbose=
		diffstat=
		;;
	--whitespace)
		shift
		git_am_opt="$git_am_opt --whitespace=$1"
		case "$1" in
		fix|strip)
			force_rebase=t
			;;
		esac
		;;
	--ignore-whitespace)
		git_am_opt="$git_am_opt $1"
		;;
	--committer-date-is-author-date|--ignore-date)
		git_am_opt="$git_am_opt $1"
		force_rebase=t
		;;
	-C)
		shift
		git_am_opt="$git_am_opt -C$1"
		;;
	--root)
		rebase_root=t
		;;
	-f|--no-ff)
		force_rebase=t
		;;
	--rerere-autoupdate|--no-rerere-autoupdate)
		allow_rerere_autoupdate="$1"
		;;
	--)
		shift
		break
		;;
	esac
	shift
done
test $# -gt 2 && usage

if test -n "$action"
then
	test -z "$in_progress" && die "No rebase in progress?"
	# Only interactive rebase uses detailed reflog messages
	if test "$type" = interactive && test "$GIT_REFLOG_ACTION" = rebase
	then
		GIT_REFLOG_ACTION="rebase -i ($action)"
		export GIT_REFLOG_ACTION
	fi
fi

case "$action" in
continue)
	# Sanity check
	git rev-parse --verify HEAD >/dev/null ||
		die "Cannot read HEAD"
	git update-index --ignore-submodules --refresh &&
	git diff-files --quiet --ignore-submodules || {
		echo "You must edit all merge conflicts and then"
		echo "mark them as resolved using git add"
		exit 1
	}
	read_basic_state
	run_specific_rebase
	;;
skip)
	output git reset --hard HEAD || exit $?
	read_basic_state
	run_specific_rebase
	;;
abort)
	git rerere clear
	read_basic_state
	case "$head_name" in
	refs/*)
		git symbolic-ref -m "rebase: aborting" HEAD $head_name ||
		die "Could not move back to $head_name"
		;;
	esac
	output git reset --hard $orig_head
	rm -r "$state_dir"
	exit
	;;
esac

# Make sure no rebase is in progress
if test -n "$in_progress"
then
	die '
It seems that there is already a '"${state_dir##*/}"' directory, and
I wonder if you are in the middle of another rebase.  If that is the
case, please try
	git rebase (--continue | --abort | --skip)
If that is not the case, please
	rm -fr '"$state_dir"'
and run me again.  I am stopping in case you still have something
valuable there.'
fi

if test -n "$interactive_rebase"
then
	type=interactive
	state_dir="$merge_dir"
elif test -n "$do_merge"
then
	type=merge
	state_dir="$merge_dir"
else
	type=am
	state_dir="$apply_dir"
fi

if test -z "$rebase_root"
then
	case "$#" in
	0)
		if ! upstream_name=$(git rev-parse --symbolic-full-name \
			--verify -q @{upstream} 2>/dev/null)
		then
			. git-parse-remote
			error_on_missing_default_upstream "rebase" "rebase" \
				"against" "git rebase <branch>"
		fi
		;;
	*)	upstream_name="$1"
		shift
		;;
	esac
	upstream=`git rev-parse --verify "${upstream_name}^0"` ||
	die "invalid upstream $upstream_name"
	upstream_arg="$upstream_name"
else
	test -z "$onto" && die "You must specify --onto when using --root"
	unset upstream_name
	unset upstream
	upstream_arg=--root
fi

# Make sure the branch to rebase onto is valid.
onto_name=${onto-"$upstream_name"}
case "$onto_name" in
*...*)
	if	left=${onto_name%...*} right=${onto_name#*...} &&
		onto=$(git merge-base --all ${left:-HEAD} ${right:-HEAD})
	then
		case "$onto" in
		?*"$LF"?*)
			die "$onto_name: there are more than one merge bases"
			;;
		'')
			die "$onto_name: there is no merge base"
			;;
		esac
	else
		die "$onto_name: there is no merge base"
	fi
	;;
*)
	onto=$(git rev-parse --verify "${onto_name}^0") ||
	die "Does not point to a valid commit: $onto_name"
	;;
esac

# If the branch to rebase is given, that is the branch we will rebase
# $branch_name -- branch being rebased, or HEAD (already detached)
# $orig_head -- commit object name of tip of the branch before rebasing
# $head_name -- refs/heads/<that-branch> or "detached HEAD"
switch_to=
case "$#" in
1)
	# Is it "rebase other $branchname" or "rebase other $commit"?
	branch_name="$1"
	switch_to="$1"

	if git show-ref --verify --quiet -- "refs/heads/$1" &&
	   orig_head=$(git rev-parse -q --verify "refs/heads/$1")
	then
		head_name="refs/heads/$1"
	elif orig_head=$(git rev-parse -q --verify "$1")
	then
		head_name="detached HEAD"
	else
		die "fatal: no such branch: $1"
	fi
	;;
*)
	# Do not need to switch branches, we are already on it.
	if branch_name=`git symbolic-ref -q HEAD`
	then
		head_name=$branch_name
		branch_name=`expr "z$branch_name" : 'zrefs/heads/\(.*\)'`
	else
		head_name="detached HEAD"
		branch_name=HEAD ;# detached
	fi
	orig_head=$(git rev-parse --verify "${branch_name}^0") || exit
	;;
esac

require_clean_work_tree "rebase" "Please commit or stash them."

# Now we are rebasing commits $upstream..$orig_head (or with --root,
# everything leading up to $orig_head) on top of $onto

# Check if we are already based on $onto with linear history,
# but this should be done only when upstream and onto are the same
# and if this is not an interactive rebase.
mb=$(git merge-base "$onto" "$orig_head")
if test "$type" != interactive && test "$upstream" = "$onto" &&
	test "$mb" = "$onto" &&
	# linear history?
	! (git rev-list --parents "$onto".."$orig_head" | sane_grep " .* ") > /dev/null
then
	if test -z "$force_rebase"
	then
		# Lazily switch to the target branch if needed...
		test -z "$switch_to" || git checkout "$switch_to" --
		say "Current branch $branch_name is up to date."
		exit 0
	else
		say "Current branch $branch_name is up to date, rebase forced."
	fi
fi

# If a hook exists, give it a chance to interrupt
run_pre_rebase_hook "$upstream_arg" "$@"

if test -n "$diffstat"
then
	if test -n "$verbose"
	then
		echo "Changes from $mb to $onto:"
	fi
	# We want color (if set), but no pager
	GIT_PAGER='' git diff --stat --summary "$mb" "$onto"
fi

test "$type" = interactive && run_specific_rebase

# Detach HEAD and reset the tree
say "First, rewinding head to replay your work on top of it..."
git checkout -q "$onto^0" || die "could not detach HEAD"
git update-ref ORIG_HEAD $orig_head

# If the $onto is a proper descendant of the tip of the branch, then
# we just fast-forwarded.
if test "$mb" = "$orig_head"
then
	say "Fast-forwarded $branch_name to $onto_name."
	move_to_original_branch
	exit 0
fi

if test -n "$rebase_root"
then
	revisions="$onto..$orig_head"
else
	revisions="$upstream..$orig_head"
fi

run_specific_rebase
