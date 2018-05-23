#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

SUBDIRECTORY_OK=Yes
OPTIONS_KEEPDASHDASH=
OPTIONS_STUCKLONG=t
OPTIONS_SPEC="\
git rebase [-i] [options] [--exec <cmd>] [--onto <newbase>] [<upstream>] [<branch>]
git rebase [-i] [options] [--exec <cmd>] [--onto <newbase>] --root [<branch>]
git rebase --continue | --abort | --skip | --edit-todo
--
 Available options are
v,verbose!         display a diffstat of what changed upstream
q,quiet!           be quiet. implies --no-stat
autostash          automatically stash/stash pop before and after
fork-point         use 'merge-base --fork-point' to refine upstream
onto=!             rebase onto given branch instead of upstream
r,rebase-merges?   try to rebase merges instead of skipping them
p,preserve-merges! try to recreate merges instead of ignoring them
s,strategy=!       use the given merge strategy
no-ff!             cherry-pick all commits, even if unchanged
m,merge!           use merging strategies to rebase
i,interactive!     let the user edit the list of commits to rebase
x,exec=!           add exec lines after each commit of the editable list
k,keep-empty	   preserve empty commits during rebase
allow-empty-message allow rebasing commits with empty messages
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
signoff            passed to 'git am'
whitespace=!       passed to 'git apply'
ignore-whitespace! passed to 'git apply'
C=!                passed to 'git apply'
S,gpg-sign?        GPG-sign commits
 Actions:
continue!          continue
abort!             abort and check out the original branch
skip!              skip current patch and continue
edit-todo!         edit the todo list during an interactive rebase
quit!              abort but keep HEAD where it is
show-current-patch! show the patch file being applied or merged
"
. git-sh-setup
set_reflog_action rebase
require_work_tree_exists
cd_to_toplevel

LF='
'
ok_to_skip_pre_rebase=
resolvemsg="
$(gettext 'Resolve all conflicts manually, mark them as resolved with
"git add/rm <conflicted_files>", then run "git rebase --continue".
You can instead skip this commit: run "git rebase --skip".
To abort and get back to the state before "git rebase", run "git rebase --abort".')
"
squash_onto=
unset onto
unset restrict_revision
cmd=
strategy=
strategy_opts=
do_merge=
merge_dir="$GIT_DIR"/rebase-merge
apply_dir="$GIT_DIR"/rebase-apply
verbose=
diffstat=
test "$(git config --bool rebase.stat)" = true && diffstat=t
autostash="$(git config --bool rebase.autostash || echo false)"
fork_point=auto
git_am_opt=
git_format_patch_opt=
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
rebase_merges=
rebase_cousins=
preserve_merges=
autosquash=
keep_empty=
allow_empty_message=
signoff=
test "$(git config --bool rebase.autosquash)" = "true" && autosquash=t
case "$(git config --bool commit.gpgsign)" in
true)	gpg_sign_opt=-S ;;
*)	gpg_sign_opt= ;;
esac

read_basic_state () {
	test -f "$state_dir/head-name" &&
	test -f "$state_dir/onto" &&
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
	test -f "$state_dir"/gpg_sign_opt &&
		gpg_sign_opt="$(cat "$state_dir"/gpg_sign_opt)"
	test -f "$state_dir"/signoff && {
		signoff="$(cat "$state_dir"/signoff)"
		force_rebase=t
	}
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
	test -n "$gpg_sign_opt" && echo "$gpg_sign_opt" > "$state_dir"/gpg_sign_opt
	test -n "$signoff" && echo "$signoff" >"$state_dir"/signoff
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
		die "$(eval_gettext "Could not move back to \$head_name")"
		;;
	esac
}

apply_autostash () {
	if test -f "$state_dir/autostash"
	then
		stash_sha1=$(cat "$state_dir/autostash")
		if git stash apply $stash_sha1 >/dev/null 2>&1
		then
			echo "$(gettext 'Applied autostash.')" >&2
		else
			git stash store -m "autostash" -q $stash_sha1 ||
			die "$(eval_gettext "Cannot store \$stash_sha1")"
			gettext 'Applying autostash resulted in conflicts.
Your changes are safe in the stash.
You can run "git stash pop" or "git stash drop" at any time.
' >&2
		fi
	fi
}

finish_rebase () {
	rm -f "$(git rev-parse --git-path REBASE_HEAD)"
	apply_autostash &&
	{ git gc --auto || true; } &&
	rm -rf "$state_dir"
}

run_specific_rebase () {
	if [ "$interactive_rebase" = implied ]; then
		GIT_EDITOR=:
		export GIT_EDITOR
		autosquash=
	fi
	. git-rebase--$type
	git_rebase__$type${preserve_merges:+__preserve_merges}
	ret=$?
	if test $ret -eq 0
	then
		finish_rebase
	elif test $ret -eq 2 # special exit status for rebase -i
	then
		apply_autostash &&
		rm -rf "$state_dir" &&
		die "Nothing to do"
	fi
	exit $ret
}

run_pre_rebase_hook () {
	if test -z "$ok_to_skip_pre_rebase" &&
	   test -x "$(git rev-parse --git-path hooks/pre-rebase)"
	then
		"$(git rev-parse --git-path hooks/pre-rebase)" ${1+"$@"} ||
		die "$(gettext "The pre-rebase hook refused to rebase.")"
	fi
}

test -f "$apply_dir"/applying &&
	die "$(gettext "It looks like 'git am' is in progress. Cannot rebase.")"

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
	--continue|--skip|--abort|--quit|--edit-todo|--show-current-patch)
		test $total_argc -eq 2 || usage
		action=${1##--}
		;;
	--onto=*)
		onto="${1#--onto=}"
		;;
	--exec=*)
		cmd="${cmd}exec ${1#--exec=}${LF}"
		test -z "$interactive_rebase" && interactive_rebase=implied
		;;
	--interactive)
		interactive_rebase=explicit
		;;
	--keep-empty)
		keep_empty=yes
		;;
	--allow-empty-message)
		allow_empty_message=--allow-empty-message
		;;
	--no-keep-empty)
		keep_empty=
		;;
	--rebase-merges)
		rebase_merges=t
		test -z "$interactive_rebase" && interactive_rebase=implied
		;;
	--rebase-merges=*)
		rebase_merges=t
		case "${1#*=}" in
		rebase-cousins) rebase_cousins=t;;
		no-rebase-cousins) rebase_cousins=;;
		*) die "Unknown mode: $1";;
		esac
		test -z "$interactive_rebase" && interactive_rebase=implied
		;;
	--preserve-merges)
		preserve_merges=t
		test -z "$interactive_rebase" && interactive_rebase=implied
		;;
	--autosquash)
		autosquash=t
		;;
	--no-autosquash)
		autosquash=
		;;
	--fork-point)
		fork_point=t
		;;
	--no-fork-point)
		fork_point=
		;;
	--merge)
		do_merge=t
		;;
	--strategy-option=*)
		strategy_opts="$strategy_opts $(git rev-parse --sq-quote "--${1#--strategy-option=}")"
		do_merge=t
		test -z "$strategy" && strategy=recursive
		;;
	--strategy=*)
		strategy="${1#--strategy=}"
		do_merge=t
		;;
	--no-stat)
		diffstat=
		;;
	--stat)
		diffstat=t
		;;
	--autostash)
		autostash=true
		;;
	--no-autostash)
		autostash=false
		;;
	--verbose)
		verbose=t
		diffstat=t
		GIT_QUIET=
		;;
	--quiet)
		GIT_QUIET=t
		git_am_opt="$git_am_opt -q"
		verbose=
		diffstat=
		;;
	--whitespace=*)
		git_am_opt="$git_am_opt --whitespace=${1#--whitespace=}"
		case "${1#--whitespace=}" in
		fix|strip)
			force_rebase=t
			;;
		esac
		;;
	--ignore-whitespace)
		git_am_opt="$git_am_opt $1"
		;;
	--signoff)
		signoff=--signoff
		;;
	--no-signoff)
		signoff=
		;;
	--committer-date-is-author-date|--ignore-date)
		git_am_opt="$git_am_opt $1"
		force_rebase=t
		;;
	-C*)
		git_am_opt="$git_am_opt $1"
		;;
	--root)
		rebase_root=t
		;;
	--force-rebase|--no-ff)
		force_rebase=t
		;;
	--rerere-autoupdate|--no-rerere-autoupdate)
		allow_rerere_autoupdate="$1"
		;;
	--gpg-sign)
		gpg_sign_opt=-S
		;;
	--gpg-sign=*)
		gpg_sign_opt="-S${1#--gpg-sign=}"
		;;
	--)
		shift
		break
		;;
	*)
		usage
		;;
	esac
	shift
done
test $# -gt 2 && usage

if test -n "$action"
then
	test -z "$in_progress" && die "$(gettext "No rebase in progress?")"
	# Only interactive rebase uses detailed reflog messages
	if test "$type" = interactive && test "$GIT_REFLOG_ACTION" = rebase
	then
		GIT_REFLOG_ACTION="rebase -i ($action)"
		export GIT_REFLOG_ACTION
	fi
fi

if test "$action" = "edit-todo" && test "$type" != "interactive"
then
	die "$(gettext "The --edit-todo action can only be used during interactive rebase.")"
fi

case "$action" in
continue)
	# Sanity check
	git rev-parse --verify HEAD >/dev/null ||
		die "$(gettext "Cannot read HEAD")"
	git update-index --ignore-submodules --refresh &&
	git diff-files --quiet --ignore-submodules || {
		echo "$(gettext "You must edit all merge conflicts and then
mark them as resolved using git add")"
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
		die "$(eval_gettext "Could not move back to \$head_name")"
		;;
	esac
	output git reset --hard $orig_head
	finish_rebase
	exit
	;;
quit)
	exec rm -rf "$state_dir"
	;;
edit-todo)
	run_specific_rebase
	;;
show-current-patch)
	run_specific_rebase
	die "BUG: run_specific_rebase is not supposed to return here"
	;;
esac

# Make sure no rebase is in progress
if test -n "$in_progress"
then
	state_dir_base=${state_dir##*/}
	cmd_live_rebase="git rebase (--continue | --abort | --skip)"
	cmd_clear_stale_rebase="rm -fr \"$state_dir\""
	die "
$(eval_gettext 'It seems that there is already a $state_dir_base directory, and
I wonder if you are in the middle of another rebase.  If that is the
case, please try
	$cmd_live_rebase
If that is not the case, please
	$cmd_clear_stale_rebase
and run me again.  I am stopping in case you still have something
valuable there.')"
fi

if test -n "$rebase_root" && test -z "$onto"
then
	test -z "$interactive_rebase" && interactive_rebase=implied
fi

if test -n "$keep_empty"
then
	test -z "$interactive_rebase" && interactive_rebase=implied
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

if test -t 2 && test -z "$GIT_QUIET"
then
	git_format_patch_opt="$git_format_patch_opt --progress"
fi

if test -n "$signoff"
then
	test -n "$preserve_merges" &&
		die "$(gettext "error: cannot combine '--signoff' with '--preserve-merges'")"
	git_am_opt="$git_am_opt $signoff"
	force_rebase=t
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
				"against" "git rebase $(gettext '<branch>')"
		fi

		test "$fork_point" = auto && fork_point=t
		;;
	*)	upstream_name="$1"
		if test "$upstream_name" = "-"
		then
			upstream_name="@{-1}"
		fi
		shift
		;;
	esac
	upstream=$(peel_committish "${upstream_name}") ||
	die "$(eval_gettext "invalid upstream '\$upstream_name'")"
	upstream_arg="$upstream_name"
else
	if test -z "$onto"
	then
		empty_tree=$(git hash-object -t tree /dev/null)
		onto=$(git commit-tree $empty_tree </dev/null)
		squash_onto="$onto"
	fi
	unset upstream_name
	unset upstream
	test $# -gt 1 && usage
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
			die "$(eval_gettext "\$onto_name: there are more than one merge bases")"
			;;
		'')
			die "$(eval_gettext "\$onto_name: there is no merge base")"
			;;
		esac
	else
		die "$(eval_gettext "\$onto_name: there is no merge base")"
	fi
	;;
*)
	onto=$(peel_committish "$onto_name") ||
	die "$(eval_gettext "Does not point to a valid commit: \$onto_name")"
	;;
esac

# If the branch to rebase is given, that is the branch we will rebase
# $branch_name -- branch/commit being rebased, or HEAD (already detached)
# $orig_head -- commit object name of tip of the branch before rebasing
# $head_name -- refs/heads/<that-branch> or "detached HEAD"
switch_to=
case "$#" in
1)
	# Is it "rebase other $branchname" or "rebase other $commit"?
	branch_name="$1"
	switch_to="$1"

	# Is it a local branch?
	if git show-ref --verify --quiet -- "refs/heads/$branch_name" &&
	   orig_head=$(git rev-parse -q --verify "refs/heads/$branch_name")
	then
		head_name="refs/heads/$branch_name"
	# If not is it a valid ref (branch or commit)?
	elif orig_head=$(git rev-parse -q --verify "$branch_name")
	then
		head_name="detached HEAD"

	else
		die "$(eval_gettext "fatal: no such branch/commit '\$branch_name'")"
	fi
	;;
0)
	# Do not need to switch branches, we are already on it.
	if branch_name=$(git symbolic-ref -q HEAD)
	then
		head_name=$branch_name
		branch_name=$(expr "z$branch_name" : 'zrefs/heads/\(.*\)')
	else
		head_name="detached HEAD"
		branch_name=HEAD
	fi
	orig_head=$(git rev-parse --verify HEAD) || exit
	;;
*)
	die "BUG: unexpected number of arguments left to parse"
	;;
esac

if test "$fork_point" = t
then
	new_upstream=$(git merge-base --fork-point "$upstream_name" \
			"${switch_to:-HEAD}")
	if test -n "$new_upstream"
	then
		restrict_revision=$new_upstream
	fi
fi

if test "$autostash" = true && ! (require_clean_work_tree) 2>/dev/null
then
	stash_sha1=$(git stash create "autostash") ||
	die "$(gettext 'Cannot autostash')"

	mkdir -p "$state_dir" &&
	echo $stash_sha1 >"$state_dir/autostash" &&
	stash_abbrev=$(git rev-parse --short $stash_sha1) &&
	echo "$(eval_gettext 'Created autostash: $stash_abbrev')" &&
	git reset --hard
fi

require_clean_work_tree "rebase" "$(gettext "Please commit or stash them.")"

# Now we are rebasing commits $upstream..$orig_head (or with --root,
# everything leading up to $orig_head) on top of $onto

# Check if we are already based on $onto with linear history,
# but this should be done only when upstream and onto are the same
# and if this is not an interactive rebase.
mb=$(git merge-base "$onto" "$orig_head")
if test "$type" != interactive && test "$upstream" = "$onto" &&
	test "$mb" = "$onto" && test -z "$restrict_revision" &&
	# linear history?
	! (git rev-list --parents "$onto".."$orig_head" | sane_grep " .* ") > /dev/null
then
	if test -z "$force_rebase"
	then
		# Lazily switch to the target branch if needed...
		test -z "$switch_to" ||
		GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION: checkout $switch_to" \
			git checkout -q "$switch_to" --
		if test "$branch_name" = "HEAD" &&
			 ! git symbolic-ref -q HEAD
		then
			say "$(eval_gettext "HEAD is up to date.")"
		else
			say "$(eval_gettext "Current branch \$branch_name is up to date.")"
		fi
		finish_rebase
		exit 0
	else
		if test "$branch_name" = "HEAD" &&
			 ! git symbolic-ref -q HEAD
		then
			say "$(eval_gettext "HEAD is up to date, rebase forced.")"
		else
			say "$(eval_gettext "Current branch \$branch_name is up to date, rebase forced.")"
		fi
	fi
fi

# If a hook exists, give it a chance to interrupt
run_pre_rebase_hook "$upstream_arg" "$@"

if test -n "$diffstat"
then
	if test -n "$verbose"
	then
		echo "$(eval_gettext "Changes from \$mb to \$onto:")"
	fi
	# We want color (if set), but no pager
	GIT_PAGER='' git diff --stat --summary "$mb" "$onto"
fi

test "$type" = interactive && run_specific_rebase

# Detach HEAD and reset the tree
say "$(gettext "First, rewinding head to replay your work on top of it...")"

GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION: checkout $onto_name" \
	git checkout -q "$onto^0" || die "could not detach HEAD"
git update-ref ORIG_HEAD $orig_head

# If the $onto is a proper descendant of the tip of the branch, then
# we just fast-forwarded.
if test "$mb" = "$orig_head"
then
	say "$(eval_gettext "Fast-forwarded \$branch_name to \$onto_name.")"
	move_to_original_branch
	finish_rebase
	exit 0
fi

if test -n "$rebase_root"
then
	revisions="$onto..$orig_head"
else
	revisions="${restrict_revision-$upstream}..$orig_head"
fi

run_specific_rebase
