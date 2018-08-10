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

git_rebase__interactive () {
	initiate_action "$action"
	ret=$?
	if test $ret = 0; then
		return 0
	fi

	test -n "$keep_empty" && keep_empty="--keep-empty"
	test -n "$rebase_merges" && rebase_merges="--rebase-merges"
	test -n "$rebase_cousins" && rebase_cousins="--rebase-cousins"
	test -n "$autosquash" && autosquash="--autosquash"
	test -n "$verbose" && verbose="--verbose"
	test -n "$force_rebase" && force_rebase="--no-ff"
	test -n "$restrict_revisions" && restrict_revisions="--restrict-revisions=^$restrict_revisions"
	test -n "$upstream" && upstream="--upstream=$upstream"
	test -n "$onto" && onto="--onto=$onto"
	test -n "$squash_onto" && squash_onto="--squash-onto=$squash_onto"
	test -n "$onto_name" && onto_name="--onto-name=$onto_name"
	test -n "$head_name" && head_name="--head-name=$head_name"
	test -n "$strategy" && strategy="--strategy=$strategy"
	test -n "$strategy_opts" && strategy_opts="--strategy-opts=$strategy_opts"
	test -n "$switch_to" && switch_to="--switch-to=$switch_to"
	test -n "$cmd" && cmd="--cmd=$cmd"

	exec git rebase--interactive2 "$keep_empty" "$rebase_merges" "$rebase_cousins" \
		"$upstream" "$onto" "$squash_onto" "$restrict_revision" \
		"$allow_empty_message" "$autosquash" "$verbose" \
		"$force_rebase" "$onto_name" "$head_name" "$strategy" \
		"$strategy_opts" "$cmd" "$switch_to" \
		"$allow_rerere_autoupdate" "$gpg_sign_opt" "$signoff"
}
