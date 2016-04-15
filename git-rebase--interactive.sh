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

# The rebase command lines that have already been processed.  A line
# is moved here when it is first handled, before any associated user
# actions.
done="$state_dir"/done

# The commit message that is planned to be used for any changes that
# need to be committed following a user interaction.
msg="$state_dir"/message

# The file into which is accumulated the suggested commit message for
# squash/fixup commands.  When the first of a series of squash/fixups
# is seen, the file is created and the commit message from the
# previous commit and from the first squash/fixup commit are written
# to it.  The commit message for each subsequent squash/fixup commit
# is appended to the file as it is processed.
#
# The first line of the file is of the form
#     # This is a combination of $count commits.
# where $count is the number of commits whose messages have been
# written to the file so far (including the initial "pick" commit).
# Each time that a commit message is processed, this line is read and
# updated.  It is deleted just before the combined commit is made.
squash_msg="$state_dir"/message-squash

# If the current series of squash/fixups has not yet included a squash
# command, then this file exists and holds the commit message of the
# original "pick" commit.  (If the series ends without a "squash"
# command, then this can be used as the commit message of the combined
# commit without opening the editor.)
fixup_msg="$state_dir"/message-fixup

# $rewritten is the name of a directory containing files for each
# commit that is reachable by at least one merge base of $head and
# $upstream. They are not necessarily rewritten, but their children
# might be.  This ensures that commits on merged, but otherwise
# unrelated side branches are left alone. (Think "X" in the man page's
# example.)
rewritten="$state_dir"/rewritten

dropped="$state_dir"/dropped

end="$state_dir"/end
msgnum="$state_dir"/msgnum

# A script to set the GIT_AUTHOR_NAME, GIT_AUTHOR_EMAIL, and
# GIT_AUTHOR_DATE that will be used for the commit that is currently
# being rebased.
author_script="$state_dir"/author-script

# When an "edit" rebase command is being processed, the SHA1 of the
# commit to be edited is recorded in this file.  When "git rebase
# --continue" is executed, if there are any staged changes then they
# will be amended to the HEAD commit, but only provided the HEAD
# commit is still the commit to be edited.  When any other rebase
# command is processed, this file is deleted.
amend="$state_dir"/amend

# For the post-rewrite hook, we make a list of rewritten commits and
# their new sha1s.  The rewritten-pending list keeps the sha1s of
# commits that have been processed, but not committed yet,
# e.g. because they are waiting for a 'squash' command.
rewritten_list="$state_dir"/rewritten-list
rewritten_pending="$state_dir"/rewritten-pending

# Work around Git for Windows' Bash whose "read" does not strip CRLF
# and leaves CR at the end instead.
cr=$(printf "\015")

strategy_args=${strategy:+--strategy=$strategy}
test -n "$strategy_opts" &&
eval '
	for strategy_opt in '"$strategy_opts"'
	do
		strategy_args="$strategy_args -X$(git rev-parse --sq-quote "${strategy_opt#--}")"
	done
'

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

warn () {
	printf '%s\n' "$*" >&2
}

# Output the commit message for the specified commit.
commit_message () {
	git cat-file commit "$1" | sed "1,/^$/d"
}

orig_reflog_action="$GIT_REFLOG_ACTION"

comment_for_reflog () {
	case "$orig_reflog_action" in
	''|rebase*)
		GIT_REFLOG_ACTION="rebase -i ($1)"
		export GIT_REFLOG_ACTION
		;;
	esac
}

last_count=
mark_action_done () {
	sed -e 1q < "$todo" >> "$done"
	sed -e 1d < "$todo" >> "$todo".new
	mv -f "$todo".new "$todo"
	new_count=$(( $(git stripspace --strip-comments <"$done" | wc -l) ))
	echo $new_count >"$msgnum"
	total=$(($new_count + $(git stripspace --strip-comments <"$todo" | wc -l)))
	echo $total >"$end"
	if test "$last_count" != "$new_count"
	then
		last_count=$new_count
		eval_gettext "Rebasing (\$new_count/\$total)"; printf "\r"
		test -z "$verbose" || echo
	fi
}

# Put the last action marked done at the beginning of the todo list
# again. If there has not been an action marked done yet, leave the list of
# items on the todo list unchanged.
reschedule_last_action () {
	tail -n 1 "$done" | cat - "$todo" >"$todo".new
	sed -e \$d <"$done" >"$done".new
	mv -f "$todo".new "$todo"
	mv -f "$done".new "$done"
}

append_todo_help () {
	gettext "
Commands:
p, pick = use commit
r, reword = use commit, but edit the commit message
e, edit = use commit, but stop for amending
s, squash = use commit, but meld into previous commit
f, fixup = like \"squash\", but discard this commit's log message
x, exec = run command (the rest of the line) using shell
d, drop = remove commit

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

make_patch () {
	sha1_and_parents="$(git rev-list --parents -1 "$1")"
	case "$sha1_and_parents" in
	?*' '?*' '?*)
		git diff --cc $sha1_and_parents
		;;
	?*' '?*)
		git diff-tree -p "$1^!"
		;;
	*)
		echo "Root commit"
		;;
	esac > "$state_dir"/patch
	test -f "$msg" ||
		commit_message "$1" > "$msg"
	test -f "$author_script" ||
		get_author_ident_from_commit "$1" > "$author_script"
}

die_with_patch () {
	echo "$1" > "$state_dir"/stopped-sha
	make_patch "$1"
	die "$2"
}

exit_with_patch () {
	echo "$1" > "$state_dir"/stopped-sha
	make_patch $1
	git rev-parse --verify HEAD > "$amend"
	gpg_sign_opt_quoted=${gpg_sign_opt:+$(git rev-parse --sq-quote "$gpg_sign_opt")}
	warn "$(eval_gettext "\
You can amend the commit now, with

	git commit --amend \$gpg_sign_opt_quoted

Once you are satisfied with your changes, run

	git rebase --continue")"
	warn
	exit $2
}

die_abort () {
	apply_autostash
	rm -rf "$state_dir"
	die "$1"
}

has_action () {
	test -n "$(git stripspace --strip-comments <"$1")"
}

is_empty_commit() {
	tree=$(git rev-parse -q --verify "$1"^{tree} 2>/dev/null) || {
		sha1=$1
		die "$(eval_gettext "\$sha1: not a commit that can be picked")"
	}
	ptree=$(git rev-parse -q --verify "$1"^^{tree} 2>/dev/null) ||
		ptree=4b825dc642cb6eb9a060e54bf8d69288fbee4904
	test "$tree" = "$ptree"
}

is_merge_commit()
{
	git rev-parse --verify --quiet "$1"^2 >/dev/null 2>&1
}

# Run command with GIT_AUTHOR_NAME, GIT_AUTHOR_EMAIL, and
# GIT_AUTHOR_DATE exported from the current environment.
do_with_author () {
	(
		export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE
		"$@"
	)
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

pick_one () {
	ff=--ff

	case "$1" in -n) sha1=$2; ff= ;; *) sha1=$1 ;; esac
	case "$force_rebase" in '') ;; ?*) ff= ;; esac
	output git rev-parse --verify $sha1 || die "$(eval_gettext "Invalid commit name: \$sha1")"

	if is_empty_commit "$sha1"
	then
		empty_args="--allow-empty"
	fi

	test -d "$rewritten" &&
		pick_one_preserving_merges "$@" && return
	output eval git cherry-pick \
			${gpg_sign_opt:+$(git rev-parse --sq-quote "$gpg_sign_opt")} \
			"$strategy_args" $empty_args $ff "$@"

	# If cherry-pick dies it leaves the to-be-picked commit unrecorded. Reschedule
	# previous task so this commit is not lost.
	ret=$?
	case "$ret" in [01]) ;; *) reschedule_last_action ;; esac
	return $ret
}

pick_one_preserving_merges () {
	fast_forward=t
	case "$1" in
	-n)
		fast_forward=f
		sha1=$2
		;;
	*)
		sha1=$1
		;;
	esac
	sha1=$(git rev-parse $sha1)

	if test -f "$state_dir"/current-commit
	then
		if test "$fast_forward" = t
		then
			while read current_commit
			do
				git rev-parse HEAD > "$rewritten"/$current_commit
			done <"$state_dir"/current-commit
			rm "$state_dir"/current-commit ||
				die "$(gettext "Cannot write current commit's replacement sha1")"
		fi
	fi

	echo $sha1 >> "$state_dir"/current-commit

	# rewrite parents; if none were rewritten, we can fast-forward.
	new_parents=
	pend=" $(git rev-list --parents -1 $sha1 | cut -d' ' -s -f2-)"
	if test "$pend" = " "
	then
		pend=" root"
	fi
	while [ "$pend" != "" ]
	do
		p=$(expr "$pend" : ' \([^ ]*\)')
		pend="${pend# $p}"

		if test -f "$rewritten"/$p
		then
			new_p=$(cat "$rewritten"/$p)

			# If the todo reordered commits, and our parent is marked for
			# rewriting, but hasn't been gotten to yet, assume the user meant to
			# drop it on top of the current HEAD
			if test -z "$new_p"
			then
				new_p=$(git rev-parse HEAD)
			fi

			test $p != $new_p && fast_forward=f
			case "$new_parents" in
			*$new_p*)
				;; # do nothing; that parent is already there
			*)
				new_parents="$new_parents $new_p"
				;;
			esac
		else
			if test -f "$dropped"/$p
			then
				fast_forward=f
				replacement="$(cat "$dropped"/$p)"
				test -z "$replacement" && replacement=root
				pend=" $replacement$pend"
			else
				new_parents="$new_parents $p"
			fi
		fi
	done
	case $fast_forward in
	t)
		output warn "$(eval_gettext "Fast-forward to \$sha1")"
		output git reset --hard $sha1 ||
			die "$(eval_gettext "Cannot fast-forward to \$sha1")"
		;;
	f)
		first_parent=$(expr "$new_parents" : ' \([^ ]*\)')

		if [ "$1" != "-n" ]
		then
			# detach HEAD to current parent
			output git checkout $first_parent 2> /dev/null ||
				die "$(eval_gettext "Cannot move HEAD to \$first_parent")"
		fi

		case "$new_parents" in
		' '*' '*)
			test "a$1" = a-n && die "$(eval_gettext "Refusing to squash a merge: \$sha1")"

			# redo merge
			author_script_content=$(get_author_ident_from_commit $sha1)
			eval "$author_script_content"
			msg_content="$(commit_message $sha1)"
			# No point in merging the first parent, that's HEAD
			new_parents=${new_parents# $first_parent}
			merge_args="--no-log --no-ff"
			if ! do_with_author output eval \
			'git merge ${gpg_sign_opt:+"$gpg_sign_opt"} \
				$merge_args $strategy_args -m "$msg_content" $new_parents'
			then
				printf "%s\n" "$msg_content" > "$GIT_DIR"/MERGE_MSG
				die_with_patch $sha1 "$(eval_gettext "Error redoing merge \$sha1")"
			fi
			echo "$sha1 $(git rev-parse HEAD^0)" >> "$rewritten_list"
			;;
		*)
			output eval git cherry-pick \
				${gpg_sign_opt:+$(git rev-parse --sq-quote "$gpg_sign_opt")} \
				"$strategy_args" "$@" ||
				die_with_patch $sha1 "$(eval_gettext "Could not pick \$sha1")"
			;;
		esac
		;;
	esac
}

this_nth_commit_message () {
	n=$1
	eval_gettext "This is the commit message #\${n}:"
}

skip_nth_commit_message () {
	n=$1
	eval_gettext "The commit message #\${n} will be skipped:"
}

update_squash_messages () {
	if test -f "$squash_msg"; then
		mv "$squash_msg" "$squash_msg".bak || exit
		count=$(($(sed -n \
			-e "1s/^$comment_char[^0-9]*\([0-9][0-9]*\).*/\1/p" \
			-e "q" < "$squash_msg".bak)+1))
		{
			printf '%s\n' "$comment_char $(eval_ngettext \
				"This is a combination of \$count commit." \
				"This is a combination of \$count commits." \
				$count)"
			sed -e 1d -e '2,/^./{
				/^$/d
			}' <"$squash_msg".bak
		} >"$squash_msg"
	else
		commit_message HEAD >"$fixup_msg" ||
		die "$(eval_gettext "Cannot write \$fixup_msg")"
		count=2
		{
			printf '%s\n' "$comment_char $(gettext "This is a combination of 2 commits.")"
			printf '%s\n' "$comment_char $(gettext "This is the 1st commit message:")"
			echo
			cat "$fixup_msg"
		} >"$squash_msg"
	fi
	case $1 in
	squash)
		rm -f "$fixup_msg"
		echo
		printf '%s\n' "$comment_char $(this_nth_commit_message $count)"
		echo
		commit_message $2
		;;
	fixup)
		echo
		printf '%s\n' "$comment_char $(skip_nth_commit_message $count)"
		echo
		# Change the space after the comment character to TAB:
		commit_message $2 | git stripspace --comment-lines | sed -e 's/ /	/'
		;;
	esac >>"$squash_msg"
}

peek_next_command () {
	git stripspace --strip-comments <"$todo" | sed -n -e 's/ .*//p' -e q
}

# A squash/fixup has failed.  Prepare the long version of the squash
# commit message, then die_with_patch.  This code path requires the
# user to edit the combined commit message for all commits that have
# been squashed/fixedup so far.  So also erase the old squash
# messages, effectively causing the combined commit to be used as the
# new basis for any further squash/fixups.  Args: sha1 rest
die_failed_squash() {
	sha1=$1
	rest=$2
	mv "$squash_msg" "$msg" || exit
	rm -f "$fixup_msg"
	cp "$msg" "$GIT_DIR"/MERGE_MSG || exit
	warn
	warn "$(eval_gettext "Could not apply \$sha1... \$rest")"
	die_with_patch $sha1 ""
}

flush_rewritten_pending() {
	test -s "$rewritten_pending" || return
	newsha1="$(git rev-parse HEAD^0)"
	sed "s/$/ $newsha1/" < "$rewritten_pending" >> "$rewritten_list"
	rm -f "$rewritten_pending"
}

record_in_rewritten() {
	oldsha1="$(git rev-parse $1)"
	echo "$oldsha1" >> "$rewritten_pending"

	case "$(peek_next_command)" in
	squash|s|fixup|f)
		;;
	*)
		flush_rewritten_pending
		;;
	esac
}

do_pick () {
	sha1=$1
	rest=$2
	if test "$(git rev-parse HEAD)" = "$squash_onto"
	then
		# Set the correct commit message and author info on the
		# sentinel root before cherry-picking the original changes
		# without committing (-n).  Finally, update the sentinel again
		# to include these changes.  If the cherry-pick results in a
		# conflict, this means our behaviour is similar to a standard
		# failed cherry-pick during rebase, with a dirty index to
		# resolve before manually running git commit --amend then git
		# rebase --continue.
		git commit --allow-empty --allow-empty-message --amend \
			   --no-post-rewrite -n -q -C $sha1 &&
			pick_one -n $sha1 &&
			git commit --allow-empty --allow-empty-message \
				   --amend --no-post-rewrite -n -q -C $sha1 \
				   ${gpg_sign_opt:+"$gpg_sign_opt"} ||
				   die_with_patch $sha1 "$(eval_gettext "Could not apply \$sha1... \$rest")"
	else
		pick_one $sha1 ||
			die_with_patch $sha1 "$(eval_gettext "Could not apply \$sha1... \$rest")"
	fi
}

do_next () {
	rm -f "$msg" "$author_script" "$amend" "$state_dir"/stopped-sha || exit
	read -r command sha1 rest < "$todo"
	case "$command" in
	"$comment_char"*|''|noop|drop|d)
		mark_action_done
		;;
	"$cr")
		# Work around CR left by "read" (e.g. with Git for Windows' Bash).
		mark_action_done
		;;
	pick|p)
		comment_for_reflog pick

		mark_action_done
		do_pick $sha1 "$rest"
		record_in_rewritten $sha1
		;;
	reword|r)
		comment_for_reflog reword

		mark_action_done
		do_pick $sha1 "$rest"
		git commit --amend --no-post-rewrite ${gpg_sign_opt:+"$gpg_sign_opt"} || {
			warn "$(eval_gettext "\
Could not amend commit after successfully picking \$sha1... \$rest
This is most likely due to an empty commit message, or the pre-commit hook
failed. If the pre-commit hook failed, you may need to resolve the issue before
you are able to reword the commit.")"
			exit_with_patch $sha1 1
		}
		record_in_rewritten $sha1
		;;
	edit|e)
		comment_for_reflog edit

		mark_action_done
		do_pick $sha1 "$rest"
		sha1_abbrev=$(git rev-parse --short $sha1)
		warn "$(eval_gettext "Stopped at \$sha1_abbrev... \$rest")"
		exit_with_patch $sha1 0
		;;
	squash|s|fixup|f)
		case "$command" in
		squash|s)
			squash_style=squash
			;;
		fixup|f)
			squash_style=fixup
			;;
		esac
		comment_for_reflog $squash_style

		test -f "$done" && has_action "$done" ||
			die "$(eval_gettext "Cannot '\$squash_style' without a previous commit")"

		mark_action_done
		update_squash_messages $squash_style $sha1
		author_script_content=$(get_author_ident_from_commit HEAD)
		echo "$author_script_content" > "$author_script"
		eval "$author_script_content"
		if ! pick_one -n $sha1
		then
			git rev-parse --verify HEAD >"$amend"
			die_failed_squash $sha1 "$rest"
		fi
		case "$(peek_next_command)" in
		squash|s|fixup|f)
			# This is an intermediate commit; its message will only be
			# used in case of trouble.  So use the long version:
			do_with_author output git commit --amend --no-verify -F "$squash_msg" \
				${gpg_sign_opt:+"$gpg_sign_opt"} ||
				die_failed_squash $sha1 "$rest"
			;;
		*)
			# This is the final command of this squash/fixup group
			if test -f "$fixup_msg"
			then
				do_with_author git commit --amend --no-verify -F "$fixup_msg" \
					${gpg_sign_opt:+"$gpg_sign_opt"} ||
					die_failed_squash $sha1 "$rest"
			else
				cp "$squash_msg" "$GIT_DIR"/SQUASH_MSG || exit
				rm -f "$GIT_DIR"/MERGE_MSG
				do_with_author git commit --amend --no-verify -F "$GIT_DIR"/SQUASH_MSG -e \
					${gpg_sign_opt:+"$gpg_sign_opt"} ||
					die_failed_squash $sha1 "$rest"
			fi
			rm -f "$squash_msg" "$fixup_msg"
			;;
		esac
		record_in_rewritten $sha1
		;;
	x|"exec")
		read -r command rest < "$todo"
		mark_action_done
		eval_gettextln "Executing: \$rest"
		"${SHELL:-@SHELL_PATH@}" -c "$rest" # Actual execution
		status=$?
		# Run in subshell because require_clean_work_tree can die.
		dirty=f
		(require_clean_work_tree "rebase" 2>/dev/null) || dirty=t
		if test "$status" -ne 0
		then
			warn "$(eval_gettext "Execution failed: \$rest")"
			test "$dirty" = f ||
				warn "$(gettext "and made changes to the index and/or the working tree")"

			warn "$(gettext "\
You can fix the problem, and then run

	git rebase --continue")"
			warn
			if test $status -eq 127		# command not found
			then
				status=1
			fi
			exit "$status"
		elif test "$dirty" = t
		then
			# TRANSLATORS: after these lines is a command to be issued by the user
			warn "$(eval_gettext "\
Execution succeeded: \$rest
but left changes to the index and/or the working tree
Commit or stash your changes, and then run

	git rebase --continue")"
			warn
			exit 1
		fi
		;;
	*)
		warn "$(eval_gettext "Unknown command: \$command \$sha1 \$rest")"
		fixtodo="$(gettext "Please fix this using 'git rebase --edit-todo'.")"
		if git rev-parse --verify -q "$sha1" >/dev/null
		then
			die_with_patch $sha1 "$fixtodo"
		else
			die "$fixtodo"
		fi
		;;
	esac
	test -s "$todo" && return

	comment_for_reflog finish &&
	newhead=$(git rev-parse HEAD) &&
	case $head_name in
	refs/*)
		message="$GIT_REFLOG_ACTION: $head_name onto $onto" &&
		git update-ref -m "$message" $head_name $newhead $orig_head &&
		git symbolic-ref \
		  -m "$GIT_REFLOG_ACTION: returning to $head_name" \
		  HEAD $head_name
		;;
	esac && {
		test ! -f "$state_dir"/verbose ||
			git diff-tree --stat $orig_head..HEAD
	} &&
	{
		test -s "$rewritten_list" &&
		git notes copy --for-rewrite=rebase < "$rewritten_list" ||
		true # we don't care if this copying failed
	} &&
	hook="$(git rev-parse --git-path hooks/post-rewrite)"
	if test -x "$hook" && test -s "$rewritten_list"; then
		"$hook" rebase < "$rewritten_list"
		true # we don't care if this hook failed
	fi &&
		warn "$(eval_gettext "Successfully rebased and updated \$head_name.")"

	return 1 # not failure; just to break the do_rest loop
}

# can only return 0, when the infinite loop breaks
do_rest () {
	while :
	do
		do_next || break
	done
}

# skip picking commits whose parents are unchanged
skip_unnecessary_picks () {
	fd=3
	while read -r command rest
	do
		# fd=3 means we skip the command
		case "$fd,$command" in
		3,pick|3,p)
			# pick a commit whose parent is current $onto -> skip
			sha1=${rest%% *}
			case "$(git rev-parse --verify --quiet "$sha1"^)" in
			"$onto"*)
				onto=$sha1
				;;
			*)
				fd=1
				;;
			esac
			;;
		3,"$comment_char"*|3,)
			# copy comments
			;;
		*)
			fd=1
			;;
		esac
		printf '%s\n' "$command${rest:+ }$rest" >&$fd
	done <"$todo" >"$todo.new" 3>>"$done" &&
	mv -f "$todo".new "$todo" &&
	case "$(peek_next_command)" in
	squash|s|fixup|f)
		record_in_rewritten "$onto"
		;;
	esac ||
		die "$(gettext "Could not skip unnecessary pick commands")"
}

transform_todo_ids () {
	while read -r command rest
	do
		case "$command" in
		"$comment_char"* | exec)
			# Be careful for oddball commands like 'exec'
			# that do not have a SHA-1 at the beginning of $rest.
			;;
		*)
			sha1=$(git rev-parse --verify --quiet "$@" ${rest%%[	 ]*}) &&
			rest="$sha1 ${rest#*[	 ]}"
			;;
		esac
		printf '%s\n' "$command${rest:+ }$rest"
	done <"$todo" >"$todo.new" &&
	mv -f "$todo.new" "$todo"
}

expand_todo_ids() {
	transform_todo_ids
}

collapse_todo_ids() {
	transform_todo_ids --short
}

# Rearrange the todo list that has both "pick sha1 msg" and
# "pick sha1 fixup!/squash! msg" appears in it so that the latter
# comes immediately after the former, and change "pick" to
# "fixup"/"squash".
#
# Note that if the config has specified a custom instruction format
# each log message will be re-retrieved in order to normalize the
# autosquash arrangement
rearrange_squash () {
	format=$(git config --get rebase.instructionFormat)
	# extract fixup!/squash! lines and resolve any referenced sha1's
	while read -r pick sha1 message
	do
		test -z "${format}" || message=$(git log -n 1 --format="%s" ${sha1})
		case "$message" in
		"squash! "*|"fixup! "*)
			action="${message%%!*}"
			rest=$message
			prefix=
			# skip all squash! or fixup! (but save for later)
			while :
			do
				case "$rest" in
				"squash! "*|"fixup! "*)
					prefix="$prefix${rest%%!*},"
					rest="${rest#*! }"
					;;
				*)
					break
					;;
				esac
			done
			printf '%s %s %s %s\n' "$sha1" "$action" "$prefix" "$rest"
			# if it's a single word, try to resolve to a full sha1 and
			# emit a second copy. This allows us to match on both message
			# and on sha1 prefix
			if test "${rest#* }" = "$rest"; then
				fullsha="$(git rev-parse -q --verify "$rest" 2>/dev/null)"
				if test -n "$fullsha"; then
					# prefix the action to uniquely identify this line as
					# intended for full sha1 match
					echo "$sha1 +$action $prefix $fullsha"
				fi
			fi
		esac
	done >"$1.sq" <"$1"
	test -s "$1.sq" || return

	used=
	while read -r pick sha1 message
	do
		case " $used" in
		*" $sha1 "*) continue ;;
		esac
		printf '%s\n' "$pick $sha1 $message"
		test -z "${format}" || message=$(git log -n 1 --format="%s" ${sha1})
		used="$used$sha1 "
		while read -r squash action msg_prefix msg_content
		do
			case " $used" in
			*" $squash "*) continue ;;
			esac
			emit=0
			case "$action" in
			+*)
				action="${action#+}"
				# full sha1 prefix test
				case "$msg_content" in "$sha1"*) emit=1;; esac ;;
			*)
				# message prefix test
				case "$message" in "$msg_content"*) emit=1;; esac ;;
			esac
			if test $emit = 1; then
				if test -n "${format}"
				then
					msg_content=$(git log -n 1 --format="${format}" ${squash})
				else
					msg_content="$(echo "$msg_prefix" | sed "s/,/! /g")$msg_content"
				fi
				printf '%s\n' "$action $squash $msg_content"
				used="$used$squash "
			fi
		done <"$1.sq"
	done >"$1.rearranged" <"$1"
	cat "$1.rearranged" >"$1"
	rm -f "$1.sq" "$1.rearranged"
}

# Add commands after a pick or after a squash/fixup serie
# in the todo list.
add_exec_commands () {
	{
		first=t
		while read -r insn rest
		do
			case $insn in
			pick)
				test -n "$first" ||
				printf "%s" "$cmd"
				;;
			esac
			printf "%s %s\n" "$insn" "$rest"
			first=
		done
		printf "%s" "$cmd"
	} <"$1" >"$1.new" &&
	mv "$1.new" "$1"
}

# Check if the SHA-1 passed as an argument is a
# correct one, if not then print $2 in "$todo".badsha
# $1: the SHA-1 to test
# $2: the line number of the input
# $3: the input filename
check_commit_sha () {
	badsha=0
	if test -z "$1"
	then
		badsha=1
	else
		sha1_verif="$(git rev-parse --verify --quiet $1^{commit})"
		if test -z "$sha1_verif"
		then
			badsha=1
		fi
	fi

	if test $badsha -ne 0
	then
		line="$(sed -n -e "${2}p" "$3")"
		warn "$(eval_gettext "\
Warning: the SHA-1 is missing or isn't a commit in the following line:
 - \$line")"
		warn
	fi

	return $badsha
}

# prints the bad commits and bad commands
# from the todolist in stdin
check_bad_cmd_and_sha () {
	retval=0
	lineno=0
	while read -r command rest
	do
		lineno=$(( $lineno + 1 ))
		case $command in
		"$comment_char"*|''|noop|x|exec)
			# Doesn't expect a SHA-1
			;;
		"$cr")
			# Work around CR left by "read" (e.g. with Git for
			# Windows' Bash).
			;;
		pick|p|drop|d|reword|r|edit|e|squash|s|fixup|f)
			if ! check_commit_sha "${rest%%[ 	]*}" "$lineno" "$1"
			then
				retval=1
			fi
			;;
		*)
			line="$(sed -n -e "${lineno}p" "$1")"
			warn "$(eval_gettext "\
Warning: the command isn't recognized in the following line:
 - \$line")"
			warn
			retval=1
			;;
		esac
	done <"$1"
	return $retval
}

# Print the list of the SHA-1 of the commits
# from stdin to stdout
todo_list_to_sha_list () {
	git stripspace --strip-comments |
	while read -r command sha1 rest
	do
		case $command in
		"$comment_char"*|''|noop|x|"exec")
			;;
		*)
			long_sha=$(git rev-list --no-walk "$sha1" 2>/dev/null)
			printf "%s\n" "$long_sha"
			;;
		esac
	done
}

# Use warn for each line in stdin
warn_lines () {
	while read -r line
	do
		warn " - $line"
	done
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

# Check if the user dropped some commits by mistake
# Behaviour determined by rebase.missingCommitsCheck.
# Check if there is an unrecognized command or a
# bad SHA-1 in a command.
check_todo_list () {
	raise_error=f

	check_level=$(get_missing_commit_check_level)

	case "$check_level" in
	warn|error)
		# Get the SHA-1 of the commits
		todo_list_to_sha_list <"$todo".backup >"$todo".oldsha1
		todo_list_to_sha_list <"$todo" >"$todo".newsha1

		# Sort the SHA-1 and compare them
		sort -u "$todo".oldsha1 >"$todo".oldsha1+
		mv "$todo".oldsha1+ "$todo".oldsha1
		sort -u "$todo".newsha1 >"$todo".newsha1+
		mv "$todo".newsha1+ "$todo".newsha1
		comm -2 -3 "$todo".oldsha1 "$todo".newsha1 >"$todo".miss

		# Warn about missing commits
		if test -s "$todo".miss
		then
			test "$check_level" = error && raise_error=t

			warn "$(gettext "\
Warning: some commits may have been dropped accidentally.
Dropped commits (newer to older):")"

			# Make the list user-friendly and display
			opt="--no-walk=sorted --format=oneline --abbrev-commit --stdin"
			git rev-list $opt <"$todo".miss | warn_lines

			warn "$(gettext "\
To avoid this message, use \"drop\" to explicitly remove a commit.

Use 'git config rebase.missingCommitsCheck' to change the level of warnings.
The possible behaviours are: ignore, warn, error.")"
			warn
		fi
		;;
	ignore)
		;;
	*)
		warn "$(eval_gettext "Unrecognized setting \$check_level for option rebase.missingCommitsCheck. Ignoring.")"
		;;
	esac

	if ! check_bad_cmd_and_sha "$todo"
	then
		raise_error=t
	fi

	if test $raise_error = t
	then
		# Checkout before the first commit of the
		# rebase: this way git rebase --continue
		# will work correctly as it expects HEAD to be
		# placed before the commit of the next action
		checkout_onto

		warn "$(gettext "You can fix this with 'git rebase --edit-todo' and then run 'git rebase --continue'.")"
		die "$(gettext "Or you can abort the rebase with 'git rebase --abort'.")"
	fi
}

# The whole contents of this file is run by dot-sourcing it from
# inside a shell function.  It used to be that "return"s we see
# below were not inside any function, and expected to return
# to the function that dot-sourced us.
#
# However, older (9.x) versions of FreeBSD /bin/sh misbehave on such a
# construct and continue to run the statements that follow such a "return".
# As a work-around, we introduce an extra layer of a function
# here, and immediately call it after defining it.
git_rebase__interactive () {

case "$action" in
continue)
	if test ! -d "$rewritten"
	then
		exec git rebase--helper ${force_rebase:+--no-ff} --continue
	fi
	# do we have anything to commit?
	if git diff-index --cached --quiet HEAD --
	then
		# Nothing to commit -- skip this commit

		test ! -f "$GIT_DIR"/CHERRY_PICK_HEAD ||
		rm "$GIT_DIR"/CHERRY_PICK_HEAD ||
		die "$(gettext "Could not remove CHERRY_PICK_HEAD")"
	else
		if ! test -f "$author_script"
		then
			gpg_sign_opt_quoted=${gpg_sign_opt:+$(git rev-parse --sq-quote "$gpg_sign_opt")}
			die "$(eval_gettext "\
You have staged changes in your working tree.
If these changes are meant to be
squashed into the previous commit, run:

  git commit --amend \$gpg_sign_opt_quoted

If they are meant to go into a new commit, run:

  git commit \$gpg_sign_opt_quoted

In both cases, once you're done, continue with:

  git rebase --continue
")"
		fi
		. "$author_script" ||
			die "$(gettext "Error trying to find the author identity to amend commit")"
		if test -f "$amend"
		then
			current_head=$(git rev-parse --verify HEAD)
			test "$current_head" = $(cat "$amend") ||
			die "$(gettext "\
You have uncommitted changes in your working tree. Please commit them
first and then run 'git rebase --continue' again.")"
			do_with_author git commit --amend --no-verify -F "$msg" -e \
				${gpg_sign_opt:+"$gpg_sign_opt"} ||
				die "$(gettext "Could not commit staged changes.")"
		else
			do_with_author git commit --no-verify -F "$msg" -e \
				${gpg_sign_opt:+"$gpg_sign_opt"} ||
				die "$(gettext "Could not commit staged changes.")"
		fi
	fi

	if test -r "$state_dir"/stopped-sha
	then
		record_in_rewritten "$(cat "$state_dir"/stopped-sha)"
	fi

	require_clean_work_tree "rebase"
	do_rest
	return 0
	;;
skip)
	git rerere clear

	if test ! -d "$rewritten"
	then
		exec git rebase--helper ${force_rebase:+--no-ff} --continue
	fi
	do_rest
	return 0
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
esac

comment_for_reflog start

if test ! -z "$switch_to"
then
	GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION: checkout $switch_to"
	output git checkout "$switch_to" -- ||
		die "$(eval_gettext "Could not checkout \$switch_to")"

	comment_for_reflog start
fi

orig_head=$(git rev-parse --verify HEAD) || die "$(gettext "No HEAD?")"
mkdir -p "$state_dir" || die "$(eval_gettext "Could not create temporary \$state_dir")"

: > "$state_dir"/interactive || die "$(gettext "Could not mark as interactive")"
write_basic_state
if test t = "$preserve_merges"
then
	if test -z "$rebase_root"
	then
		mkdir "$rewritten" &&
		for c in $(git merge-base --all $orig_head $upstream)
		do
			echo $onto > "$rewritten"/$c ||
				die "$(gettext "Could not init rewritten commits")"
		done
	else
		mkdir "$rewritten" &&
		echo $onto > "$rewritten"/root ||
			die "$(gettext "Could not init rewritten commits")"
	fi
	# No cherry-pick because our first pass is to determine
	# parents to rewrite and skipping dropped commits would
	# prematurely end our probe
	merges_option=
else
	merges_option="--no-merges --cherry-pick"
fi

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
fi
if test t != "$preserve_merges"
then
	git rebase--helper --make-script ${keep_empty:+--keep-empty} \
		$revisions ${restrict_revision+^$restrict_revision} >"$todo"
else
	format=$(git config --get rebase.instructionFormat)
	# the 'rev-list .. | sed' requires %m to parse; the instruction requires %H to parse
	git rev-list $merges_option --format="%m%H ${format:-%s}" \
		--reverse --left-right --topo-order \
		$revisions ${restrict_revision+^$restrict_revision} | \
		sed -n "s/^>//p" |
	while read -r sha1 rest
	do

		if test -z "$keep_empty" && is_empty_commit $sha1 && ! is_merge_commit $sha1
		then
			comment_out="$comment_char "
		else
			comment_out=
		fi

		if test -z "$rebase_root"
		then
			preserve=t
			for p in $(git rev-list --parents -1 $sha1 | cut -d' ' -s -f2-)
			do
				if test -f "$rewritten"/$p
				then
					preserve=f
				fi
			done
		else
			preserve=f
		fi
		if test f = "$preserve"
		then
			touch "$rewritten"/$sha1
			printf '%s\n' "${comment_out}pick $sha1 $rest" >>"$todo"
		fi
	done
fi

# Watch for commits that been dropped by --cherry-pick
if test t = "$preserve_merges"
then
	mkdir "$dropped"
	# Save all non-cherry-picked changes
	git rev-list $revisions --left-right --cherry-pick | \
		sed -n "s/^>//p" > "$state_dir"/not-cherry-picks
	# Now all commits and note which ones are missing in
	# not-cherry-picks and hence being dropped
	git rev-list $revisions |
	while read rev
	do
		if test -f "$rewritten"/$rev &&
		   ! sane_grep "$rev" "$state_dir"/not-cherry-picks >/dev/null
		then
			# Use -f2 because if rev-list is telling us this commit is
			# not worthwhile, we don't want to track its multiple heads,
			# just the history of its first-parent for others that will
			# be rebasing on top of it
			git rev-list --parents -1 $rev | cut -d' ' -s -f2 > "$dropped"/$rev
			sha1=$(git rev-list -1 $rev)
			sane_grep -v "^[a-z][a-z]* $sha1" <"$todo" > "${todo}2" ; mv "${todo}2" "$todo"
			rm "$rewritten"/$rev
		fi
	done
fi

test -s "$todo" || echo noop >> "$todo"
test -n "$autosquash" && rearrange_squash "$todo"
test -n "$cmd" && add_exec_commands "$todo"

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

check_todo_list

expand_todo_ids

test -d "$rewritten" || test -n "$force_rebase" || skip_unnecessary_picks

checkout_onto
if test -z "$rebase_root" && test ! -d "$rewritten"
then
	require_clean_work_tree "rebase"
	exec git rebase--helper ${force_rebase:+--no-ff} --continue
fi
do_rest

}
# ... and then we call the whole thing.
git_rebase__interactive
