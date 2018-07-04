# This shell script fragment is sourced by git-rebase to implement
# its interactive mode.  "git rebase --interactive" makes it easy
# to fix up commits in the middle of a series and rearrange commits.
#
# Copyright (c) 2006 Johannes E. Schindelin
#
# The original idea comes from Eric W. Biederman, in
# https://public-inbox.org/git/m1odwkyuf5.fsf_-_@ebiederm.dsl.xmission.com/
#
# The file containing rebase commands, comments, and empty lines.
# This file is created by "git rebase -i" then edited by the user.  As
# the lines are processed, they are removed from the front of this
# file and written to the tail of $done.
todo="$state_dir"/git-rebase-todo

GIT_CHERRY_PICK_HELP="$resolvemsg"
export GIT_CHERRY_PICK_HELP

# Initiate an action. If the cannot be any
# further action it  may exec a command
# or exit and not return.
#
# TODO: Consider a cleaner return model so it
# never exits and always return 0 if process
# is complete.
#
# Parameter 1 is the action to initiate.
#
# Returns 0 if the action was able to complete
# and if 1 if further processing is required.
initiate_action () {
	case "$1" in
	continue)
		exec git rebase--helper ${force_rebase:+--no-ff} $allow_empty_message \
		     --continue
		;;
	skip)
		git rerere clear
		exec git rebase--helper ${force_rebase:+--no-ff} $allow_empty_message \
		     --continue
		;;
	edit-todo)
		exec git rebase--helper --edit-todo
		;;
	show-current-patch)
		exec git show REBASE_HEAD --
		;;
	*)
		return 1 # continue
		;;
	esac
}

init_basic_state () {
	orig_head=$(git rev-parse --verify HEAD) || die "$(gettext "No HEAD?")"
	mkdir -p "$state_dir" || die "$(eval_gettext "Could not create temporary \$state_dir")"
	rm -f "$(git rev-parse --git-path REBASE_HEAD)"

	: > "$state_dir"/interactive || die "$(gettext "Could not mark as interactive")"
	write_basic_state
}

git_rebase__interactive () {
	initiate_action "$action"
	ret=$?
	if test $ret = 0; then
		return 0
	fi

	git rebase--helper --prepare-branch "$switch_to" ${verbose:+--verbose}
	init_basic_state

	git rebase--helper --make-script ${keep_empty:+--keep-empty} \
		${rebase_merges:+--rebase-merges} \
		${rebase_cousins:+--rebase-cousins} \
		${upstream:+--upstream "$upstream"} ${onto:+--onto "$onto"} \
		${squash_onto:+--squash-onto "$squash_onto"} \
		${restrict_revision:+--restrict-revision ^"$restrict_revision"} >"$todo" ||
	die "$(gettext "Could not generate todo list")"

	exec git rebase--helper --complete-action "$onto_name" "$cmd" \
		$allow_empty_message ${autosquash:+--autosquash} ${verbose:+--verbose} \
		${keep_empty:+--keep-empty} ${force_rebase:+--no-ff} \
		${upstream:+--upstream "$upstream"} ${onto:+--onto "$onto"}
}
