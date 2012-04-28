#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin

# SHORT DESCRIPTION
#
# This script makes it easy to fix up commits in the middle of a series,
# and rearrange commits.
#
# The original idea comes from Eric W. Biederman, in
# http://article.gmane.org/gmane.comp.version-control.git/22407

. git-sh-setup

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

GIT_CHERRY_PICK_HELP="$resolvemsg"
export GIT_CHERRY_PICK_HELP

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
	new_count=$(sane_grep -c '^[^#]' < "$done")
	total=$(($new_count+$(sane_grep -c '^[^#]' < "$todo")))
	if test "$last_count" != "$new_count"
	then
		last_count=$new_count
		printf "Rebasing (%d/%d)\r" $new_count $total
		test -z "$verbose" || echo
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
	git rerere
	die "$2"
}

exit_with_patch () {
	echo "$1" > "$state_dir"/stopped-sha
	make_patch $1
	git rev-parse --verify HEAD > "$amend"
	warn "You can amend the commit now, with"
	warn
	warn "	git commit --amend"
	warn
	warn "Once you are satisfied with your changes, run"
	warn
	warn "	git rebase --continue"
	warn
	exit $2
}

die_abort () {
	rm -rf "$state_dir"
	die "$1"
}

has_action () {
	sane_grep '^[^#]' "$1" >/dev/null
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
	output git rev-parse --verify $sha1 || die "Invalid commit name: $sha1"
	test -d "$rewritten" &&
		pick_one_preserving_merges "$@" && return
	output git cherry-pick $ff "$@"
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
			die "Cannot write current commit's replacement sha1"
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
		output warn "Fast-forward to $sha1"
		output git reset --hard $sha1 ||
			die "Cannot fast-forward to $sha1"
		;;
	f)
		first_parent=$(expr "$new_parents" : ' \([^ ]*\)')

		if [ "$1" != "-n" ]
		then
			# detach HEAD to current parent
			output git checkout $first_parent 2> /dev/null ||
				die "Cannot move HEAD to $first_parent"
		fi

		case "$new_parents" in
		' '*' '*)
			test "a$1" = a-n && die "Refusing to squash a merge: $sha1"

			# redo merge
			author_script_content=$(get_author_ident_from_commit $sha1)
			eval "$author_script_content"
			msg_content="$(commit_message $sha1)"
			# No point in merging the first parent, that's HEAD
			new_parents=${new_parents# $first_parent}
			if ! do_with_author output \
				git merge --no-ff ${strategy:+-s $strategy} -m \
					"$msg_content" $new_parents
			then
				printf "%s\n" "$msg_content" > "$GIT_DIR"/MERGE_MSG
				die_with_patch $sha1 "Error redoing merge $sha1"
			fi
			echo "$sha1 $(git rev-parse HEAD^0)" >> "$rewritten_list"
			;;
		*)
			output git cherry-pick "$@" ||
				die_with_patch $sha1 "Could not pick $sha1"
			;;
		esac
		;;
	esac
}

nth_string () {
	case "$1" in
	*1[0-9]|*[04-9]) echo "$1"th;;
	*1) echo "$1"st;;
	*2) echo "$1"nd;;
	*3) echo "$1"rd;;
	esac
}

update_squash_messages () {
	if test -f "$squash_msg"; then
		mv "$squash_msg" "$squash_msg".bak || exit
		count=$(($(sed -n \
			-e "1s/^# This is a combination of \(.*\) commits\./\1/p" \
			-e "q" < "$squash_msg".bak)+1))
		{
			echo "# This is a combination of $count commits."
			sed -e 1d -e '2,/^./{
				/^$/d
			}' <"$squash_msg".bak
		} >"$squash_msg"
	else
		commit_message HEAD > "$fixup_msg" || die "Cannot write $fixup_msg"
		count=2
		{
			echo "# This is a combination of 2 commits."
			echo "# The first commit's message is:"
			echo
			cat "$fixup_msg"
		} >"$squash_msg"
	fi
	case $1 in
	squash)
		rm -f "$fixup_msg"
		echo
		echo "# This is the $(nth_string $count) commit message:"
		echo
		commit_message $2
		;;
	fixup)
		echo
		echo "# The $(nth_string $count) commit message will be skipped:"
		echo
		commit_message $2 | sed -e 's/^/#	/'
		;;
	esac >>"$squash_msg"
}

peek_next_command () {
	sed -n -e "/^#/d" -e '/^$/d' -e "s/ .*//p" -e "q" < "$todo"
}

# A squash/fixup has failed.  Prepare the long version of the squash
# commit message, then die_with_patch.  This code path requires the
# user to edit the combined commit message for all commits that have
# been squashed/fixedup so far.  So also erase the old squash
# messages, effectively causing the combined commit to be used as the
# new basis for any further squash/fixups.  Args: sha1 rest
die_failed_squash() {
	mv "$squash_msg" "$msg" || exit
	rm -f "$fixup_msg"
	cp "$msg" "$GIT_DIR"/MERGE_MSG || exit
	warn
	warn "Could not apply $1... $2"
	die_with_patch $1 ""
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

do_next () {
	rm -f "$msg" "$author_script" "$amend" || exit
	read -r command sha1 rest < "$todo"
	case "$command" in
	'#'*|''|noop)
		mark_action_done
		;;
	pick|p)
		comment_for_reflog pick

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		record_in_rewritten $sha1
		;;
	reword|r)
		comment_for_reflog reword

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		git commit --amend --no-post-rewrite || {
			warn "Could not amend commit after successfully picking $sha1... $rest"
			warn "This is most likely due to an empty commit message, or the pre-commit hook"
			warn "failed. If the pre-commit hook failed, you may need to resolve the issue before"
			warn "you are able to reword the commit."
			exit_with_patch $sha1 1
		}
		record_in_rewritten $sha1
		;;
	edit|e)
		comment_for_reflog edit

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		warn "Stopped at $sha1... $rest"
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
			die "Cannot '$squash_style' without a previous commit"

		mark_action_done
		update_squash_messages $squash_style $sha1
		author_script_content=$(get_author_ident_from_commit HEAD)
		echo "$author_script_content" > "$author_script"
		eval "$author_script_content"
		output git reset --soft HEAD^
		pick_one -n $sha1 || die_failed_squash $sha1 "$rest"
		case "$(peek_next_command)" in
		squash|s|fixup|f)
			# This is an intermediate commit; its message will only be
			# used in case of trouble.  So use the long version:
			do_with_author output git commit --no-verify -F "$squash_msg" ||
				die_failed_squash $sha1 "$rest"
			;;
		*)
			# This is the final command of this squash/fixup group
			if test -f "$fixup_msg"
			then
				do_with_author git commit --no-verify -F "$fixup_msg" ||
					die_failed_squash $sha1 "$rest"
			else
				cp "$squash_msg" "$GIT_DIR"/SQUASH_MSG || exit
				rm -f "$GIT_DIR"/MERGE_MSG
				do_with_author git commit --no-verify -e ||
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
		printf 'Executing: %s\n' "$rest"
		# "exec" command doesn't take a sha1 in the todo-list.
		# => can't just use $sha1 here.
		git rev-parse --verify HEAD > "$state_dir"/stopped-sha
		${SHELL:-@SHELL_PATH@} -c "$rest" # Actual execution
		status=$?
		# Run in subshell because require_clean_work_tree can die.
		dirty=f
		(require_clean_work_tree "rebase" 2>/dev/null) || dirty=t
		if test "$status" -ne 0
		then
			warn "Execution failed: $rest"
			test "$dirty" = f ||
			warn "and made changes to the index and/or the working tree"

			warn "You can fix the problem, and then run"
			warn
			warn "	git rebase --continue"
			warn
			exit "$status"
		elif test "$dirty" = t
		then
			warn "Execution succeeded: $rest"
			warn "but left changes to the index and/or the working tree"
			warn "Commit or stash your changes, and then run"
			warn
			warn "	git rebase --continue"
			warn
			exit 1
		fi
		;;
	*)
		warn "Unknown command: $command $sha1 $rest"
		if git rev-parse --verify -q "$sha1" >/dev/null
		then
			die_with_patch $sha1 "Please fix this in the file $todo."
		else
			die "Please fix this in the file $todo."
		fi
		;;
	esac
	test -s "$todo" && return

	comment_for_reflog finish &&
	shortonto=$(git rev-parse --short $onto) &&
	newhead=$(git rev-parse HEAD) &&
	case $head_name in
	refs/*)
		message="$GIT_REFLOG_ACTION: $head_name onto $shortonto" &&
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
	if test -x "$GIT_DIR"/hooks/post-rewrite &&
		test -s "$rewritten_list"; then
		"$GIT_DIR"/hooks/post-rewrite rebase < "$rewritten_list"
		true # we don't care if this hook failed
	fi &&
	rm -rf "$state_dir" &&
	git gc --auto &&
	warn "Successfully rebased and updated $head_name."

	exit
}

do_rest () {
	while :
	do
		do_next
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
		3,#*|3,)
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
	die "Could not skip unnecessary pick commands"
}

# Rearrange the todo list that has both "pick sha1 msg" and
# "pick sha1 fixup!/squash! msg" appears in it so that the latter
# comes immediately after the former, and change "pick" to
# "fixup"/"squash".
rearrange_squash () {
	# extract fixup!/squash! lines and resolve any referenced sha1's
	while read -r pick sha1 message
	do
		case "$message" in
		"squash! "*|"fixup! "*)
			action="${message%%!*}"
			rest="${message#*! }"
			echo "$sha1 $action $rest"
			# if it's a single word, try to resolve to a full sha1 and
			# emit a second copy. This allows us to match on both message
			# and on sha1 prefix
			if test "${rest#* }" = "$rest"; then
				fullsha="$(git rev-parse -q --verify "$rest" 2>/dev/null)"
				if test -n "$fullsha"; then
					# prefix the action to uniquely identify this line as
					# intended for full sha1 match
					echo "$sha1 +$action $fullsha"
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
		used="$used$sha1 "
		while read -r squash action msg_content
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
				printf '%s\n' "$action $squash $action! $msg_content"
				used="$used$squash "
			fi
		done <"$1.sq"
	done >"$1.rearranged" <"$1"
	cat "$1.rearranged" >"$1"
	rm -f "$1.sq" "$1.rearranged"
}

case "$action" in
continue)
	# do we have anything to commit?
	if git diff-index --cached --quiet HEAD --
	then
		: Nothing to commit -- skip this
	else
		if ! test -f "$author_script"
		then
			die "You have staged changes in your working tree. If these changes are meant to be
squashed into the previous commit, run:

  git commit --amend

If they are meant to go into a new commit, run:

  git commit

In both case, once you're done, continue with:

  git rebase --continue
"
		fi
		. "$author_script" ||
			die "Error trying to find the author identity to amend commit"
		current_head=
		if test -f "$amend"
		then
			current_head=$(git rev-parse --verify HEAD)
			test "$current_head" = $(cat "$amend") ||
			die "\
You have uncommitted changes in your working tree. Please, commit them
first and then run 'git rebase --continue' again."
			git reset --soft HEAD^ ||
			die "Cannot rewind the HEAD"
		fi
		do_with_author git commit --no-verify -F "$msg" -e || {
			test -n "$current_head" && git reset --soft $current_head
			die "Could not commit staged changes."
		}
	fi

	record_in_rewritten "$(cat "$state_dir"/stopped-sha)"

	require_clean_work_tree "rebase"
	do_rest
	;;
skip)
	git rerere clear

	do_rest
	;;
esac

git var GIT_COMMITTER_IDENT >/dev/null ||
	die "You need to set your committer info first"

comment_for_reflog start

if test ! -z "$switch_to"
then
	output git checkout "$switch_to" -- ||
		die "Could not checkout $switch_to"
fi

orig_head=$(git rev-parse --verify HEAD) || die "No HEAD?"
mkdir "$state_dir" || die "Could not create temporary $state_dir"

: > "$state_dir"/interactive || die "Could not mark as interactive"
write_basic_state
if test t = "$preserve_merges"
then
	if test -z "$rebase_root"
	then
		mkdir "$rewritten" &&
		for c in $(git merge-base --all $orig_head $upstream)
		do
			echo $onto > "$rewritten"/$c ||
				die "Could not init rewritten commits"
		done
	else
		mkdir "$rewritten" &&
		echo $onto > "$rewritten"/root ||
			die "Could not init rewritten commits"
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
git rev-list $merges_option --pretty=oneline --abbrev-commit \
	--abbrev=7 --reverse --left-right --topo-order \
	$revisions | \
	sed -n "s/^>//p" |
while read -r shortsha1 rest
do
	if test t != "$preserve_merges"
	then
		printf '%s\n' "pick $shortsha1 $rest" >> "$todo"
	else
		sha1=$(git rev-parse $shortsha1)
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
			printf '%s\n' "pick $shortsha1 $rest" >> "$todo"
		fi
	fi
done

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
		if test -f "$rewritten"/$rev -a "$(sane_grep "$rev" "$state_dir"/not-cherry-picks)" = ""
		then
			# Use -f2 because if rev-list is telling us this commit is
			# not worthwhile, we don't want to track its multiple heads,
			# just the history of its first-parent for others that will
			# be rebasing on top of it
			git rev-list --parents -1 $rev | cut -d' ' -s -f2 > "$dropped"/$rev
			short=$(git rev-list -1 --abbrev-commit --abbrev=7 $rev)
			sane_grep -v "^[a-z][a-z]* $short" <"$todo" > "${todo}2" ; mv "${todo}2" "$todo"
			rm "$rewritten"/$rev
		fi
	done
fi

test -s "$todo" || echo noop >> "$todo"
test -n "$autosquash" && rearrange_squash "$todo"
cat >> "$todo" << EOF

# Rebase $shortrevisions onto $shortonto
#
# Commands:
#  p, pick = use commit
#  r, reword = use commit, but edit the commit message
#  e, edit = use commit, but stop for amending
#  s, squash = use commit, but meld into previous commit
#  f, fixup = like "squash", but discard this commit's log message
#  x, exec = run command (the rest of the line) using shell
#
# These lines can be re-ordered; they are executed from top to bottom.
#
# If you remove a line here THAT COMMIT WILL BE LOST.
# However, if you remove everything, the rebase will be aborted.
#
EOF

has_action "$todo" ||
	die_abort "Nothing to do"

cp "$todo" "$todo".backup
git_sequence_editor "$todo" ||
	die_abort "Could not execute editor"

has_action "$todo" ||
	die_abort "Nothing to do"

test -d "$rewritten" || test -n "$force_rebase" || skip_unnecessary_picks

output git checkout $onto || die_abort "could not detach HEAD"
git update-ref ORIG_HEAD $orig_head
do_rest
