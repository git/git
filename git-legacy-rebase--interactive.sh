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

comment_char=$(git config --get core.commentchar 2>/dev/null)
case "$comment_char" in
'' | auto)
	comment_char="#"
	;;
?)
	;;
*)
	comment_char=$(echo "$comment_char" | cut -c1)
	;;
esac

orig_reflog_action="$GIT_REFLOG_ACTION"

comment_for_reflog () {
	case "$orig_reflog_action" in
	''|rebase*)
		GIT_REFLOG_ACTION="rebase -i ($1)"
		export GIT_REFLOG_ACTION
		;;
	esac
}

append_todo_help () {
	gettext "
Commands:
p, pick <commit> = use commit
r, reword <commit> = use commit, but edit the commit message
e, edit <commit> = use commit, but stop for amending
s, squash <commit> = use commit, but meld into previous commit
f, fixup <commit> = like \"squash\", but discard this commit's log message
x, exec <command> = run command (the rest of the line) using shell
d, drop <commit> = remove commit
l, label <label> = label current HEAD with a name
t, reset <label> = reset HEAD to a label
m, merge [-C <commit> | -c <commit>] <label> [# <oneline>]
.       create a merge commit using the original merge commit's
.       message (or the oneline, if no original merge commit was
.       specified). Use -c <commit> to reword the commit message.

These lines can be re-ordered; they are executed from top to bottom.
" | git stripspace --comment-lines >>"$todo"

	if test $(get_missing_commit_check_level) = error
	then
		gettext "
Do not remove any line. Use 'drop' explicitly to remove a commit.
" | git stripspace --comment-lines >>"$todo"
	else
		gettext "
If you remove a line here THAT COMMIT WILL BE LOST.
" | git stripspace --comment-lines >>"$todo"
	fi
}

die_abort () {
	apply_autostash
	rm -rf "$state_dir"
	die "$1"
}

has_action () {
	test -n "$(git stripspace --strip-comments <"$1")"
}

git_sequence_editor () {
	if test -z "$GIT_SEQUENCE_EDITOR"
	then
		GIT_SEQUENCE_EDITOR="$(git config sequence.editor)"
		if [ -z "$GIT_SEQUENCE_EDITOR" ]
		then
			GIT_SEQUENCE_EDITOR="$(git var GIT_EDITOR)" || return $?
		fi
	fi

	eval "$GIT_SEQUENCE_EDITOR" '"$@"'
}

expand_todo_ids() {
	git rebase--interactive --expand-ids
}

collapse_todo_ids() {
	git rebase--interactive --shorten-ids
}

# Switch to the branch in $into and notify it in the reflog
checkout_onto () {
	GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION: checkout $onto_name"
	output git checkout $onto || die_abort "$(gettext "could not detach HEAD")"
	git update-ref ORIG_HEAD $orig_head
}

get_missing_commit_check_level () {
	check_level=$(git config --get rebase.missingCommitsCheck)
	check_level=${check_level:-ignore}
	# Don't be case sensitive
	printf '%s' "$check_level" | tr 'A-Z' 'a-z'
}

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
		exec git rebase--interactive ${force_rebase:+--no-ff} $allow_empty_message \
		     --continue
		;;
	skip)
		git rerere clear
		exec git rebase--interactive ${force_rebase:+--no-ff} $allow_empty_message \
		     --continue
		;;
	edit-todo)
		git stripspace --strip-comments <"$todo" >"$todo".new
		mv -f "$todo".new "$todo"
		collapse_todo_ids
		append_todo_help
		gettext "
You are editing the todo file of an ongoing interactive rebase.
To continue rebase after editing, run:
    git rebase --continue

" | git stripspace --comment-lines >>"$todo"

		git_sequence_editor "$todo" ||
			die "$(gettext "Could not execute editor")"
		expand_todo_ids

		exit
		;;
	show-current-patch)
		exec git show REBASE_HEAD --
		;;
	*)
		return 1 # continue
		;;
	esac
}

setup_reflog_action () {
	comment_for_reflog start

	if test ! -z "$switch_to"
	then
		GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION: checkout $switch_to"
		output git checkout "$switch_to" -- ||
			die "$(eval_gettext "Could not checkout \$switch_to")"

		comment_for_reflog start
	fi
}

init_basic_state () {
	orig_head=$(git rev-parse --verify HEAD) || die "$(gettext "No HEAD?")"
	mkdir -p "$state_dir" || die "$(eval_gettext "Could not create temporary \$state_dir")"
	rm -f "$(git rev-parse --git-path REBASE_HEAD)"

	: > "$state_dir"/interactive || die "$(gettext "Could not mark as interactive")"
	write_basic_state
}

init_revisions_and_shortrevisions () {
	shorthead=$(git rev-parse --short $orig_head)
	shortonto=$(git rev-parse --short $onto)
	if test -z "$rebase_root"
		# this is now equivalent to ! -z "$upstream"
	then
		shortupstream=$(git rev-parse --short $upstream)
		revisions=$upstream...$orig_head
		shortrevisions=$shortupstream..$shorthead
	else
		revisions=$onto...$orig_head
		shortrevisions=$shorthead
		test -z "$squash_onto" ||
		echo "$squash_onto" >"$state_dir"/squash-onto
	fi
}

complete_action() {
	test -s "$todo" || echo noop >> "$todo"
	test -z "$autosquash" || git rebase--interactive --rearrange-squash || exit
	test -n "$cmd" && git rebase--interactive --add-exec-commands --cmd "$cmd"

	todocount=$(git stripspace --strip-comments <"$todo" | wc -l)
	todocount=${todocount##* }

cat >>"$todo" <<EOF

$comment_char $(eval_ngettext \
	"Rebase \$shortrevisions onto \$shortonto (\$todocount command)" \
	"Rebase \$shortrevisions onto \$shortonto (\$todocount commands)" \
	"$todocount")
EOF
	append_todo_help
	gettext "
	However, if you remove everything, the rebase will be aborted.

	" | git stripspace --comment-lines >>"$todo"

	if test -z "$keep_empty"
	then
		printf '%s\n' "$comment_char $(gettext "Note that empty commits are commented out")" >>"$todo"
	fi


	has_action "$todo" ||
		return 2

	cp "$todo" "$todo".backup
	collapse_todo_ids
	git_sequence_editor "$todo" ||
		die_abort "$(gettext "Could not execute editor")"

	has_action "$todo" ||
		return 2

	git rebase--interactive --check-todo-list || {
		ret=$?
		checkout_onto
		exit $ret
	}

	expand_todo_ids

	test -n "$force_rebase" ||
	onto="$(git rebase--interactive --skip-unnecessary-picks)" ||
	die "Could not skip unnecessary pick commands"

	checkout_onto
	require_clean_work_tree "rebase"
	exec git rebase--interactive ${force_rebase:+--no-ff} $allow_empty_message \
	     --continue
}

git_rebase__interactive () {
	initiate_action "$action"
	ret=$?
	if test $ret = 0; then
		return 0
	fi

	setup_reflog_action
	init_basic_state

	init_revisions_and_shortrevisions

	git rebase--interactive --make-script ${keep_empty:+--keep-empty} \
		${rebase_merges:+--rebase-merges} \
		${rebase_cousins:+--rebase-cousins} \
		$revisions ${restrict_revision+^$restrict_revision} >"$todo" ||
	die "$(gettext "Could not generate todo list")"

	complete_action
}
