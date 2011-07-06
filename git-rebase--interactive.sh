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

OPTIONS_KEEPDASHDASH=
OPTIONS_SPEC="\
git-rebase [-i] [options] [--] <upstream> [<branch>]
git-rebase [-i] (--continue | --abort | --skip)
--
 Available options are
v,verbose          display a diffstat of what changed upstream
onto=              rebase onto given branch instead of upstream
p,preserve-merges  try to recreate merges instead of ignoring them
s,strategy=        use the given merge strategy
m,merge            always used (no-op)
i,interactive      always used (no-op)
 Actions:
continue           continue rebasing process
abort              abort rebasing process and restore original branch
skip               skip current patch and continue rebasing process
"

. git-sh-setup
require_work_tree

DOTEST="$GIT_DIR/rebase-merge"
TODO="$DOTEST"/git-rebase-todo
DONE="$DOTEST"/done
MSG="$DOTEST"/message
SQUASH_MSG="$DOTEST"/message-squash
REWRITTEN="$DOTEST"/rewritten
PRESERVE_MERGES=
STRATEGY=
ONTO=
VERBOSE=

GIT_CHERRY_PICK_HELP="  After resolving the conflicts,
mark the corrected paths with 'git add <paths>', and
run 'git rebase --continue'"
export GIT_CHERRY_PICK_HELP

warn () {
	echo "$*" >&2
}

output () {
	case "$VERBOSE" in
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

require_clean_work_tree () {
	# test if working tree is dirty
	git rev-parse --verify HEAD > /dev/null &&
	git update-index --ignore-submodules --refresh &&
	git diff-files --quiet --ignore-submodules &&
	git diff-index --cached --quiet HEAD --ignore-submodules -- ||
	die "Working tree is dirty"
}

ORIG_REFLOG_ACTION="$GIT_REFLOG_ACTION"

comment_for_reflog () {
	case "$ORIG_REFLOG_ACTION" in
	''|rebase*)
		GIT_REFLOG_ACTION="rebase -i ($1)"
		export GIT_REFLOG_ACTION
		;;
	esac
}

last_count=
mark_action_done () {
	sed -e 1q < "$TODO" >> "$DONE"
	sed -e 1d < "$TODO" >> "$TODO".new
	mv -f "$TODO".new "$TODO"
	count=$(grep -c '^[^#]' < "$DONE")
	total=$(($count+$(grep -c '^[^#]' < "$TODO")))
	if test "$last_count" != "$count"
	then
		last_count=$count
		printf "Rebasing (%d/%d)\r" $count $total
		test -z "$VERBOSE" || echo
	fi
}

make_patch () {
	parent_sha1=$(git rev-parse --verify "$1"^) ||
		die "Cannot get patch for $1^"
	git diff-tree -p "$parent_sha1".."$1" > "$DOTEST"/patch
	test -f "$DOTEST"/message ||
		git cat-file commit "$1" | sed "1,/^$/d" > "$DOTEST"/message
	test -f "$DOTEST"/author-script ||
		get_author_ident_from_commit "$1" > "$DOTEST"/author-script
}

die_with_patch () {
	make_patch "$1"
	git rerere
	die "$2"
}

die_abort () {
	rm -rf "$DOTEST"
	die "$1"
}

has_action () {
	grep '^[^#]' "$1" >/dev/null
}

pick_one () {
	no_ff=
	case "$1" in -n) sha1=$2; no_ff=t ;; *) sha1=$1 ;; esac
	output git rev-parse --verify $sha1 || die "Invalid commit name: $sha1"
	test -d "$REWRITTEN" &&
		pick_one_preserving_merges "$@" && return
	parent_sha1=$(git rev-parse --verify $sha1^) ||
		die "Could not get the parent of $sha1"
	current_sha1=$(git rev-parse --verify HEAD)
	if test "$no_ff$current_sha1" = "$parent_sha1"; then
		output git reset --hard $sha1
		test "a$1" = a-n && output git reset --soft $current_sha1
		sha1=$(git rev-parse --short $sha1)
		output warn Fast forward to $sha1
	else
		output git cherry-pick "$@"
	fi
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

	if test -f "$DOTEST"/current-commit
	then
		current_commit=$(cat "$DOTEST"/current-commit) &&
		git rev-parse HEAD > "$REWRITTEN"/$current_commit &&
		rm "$DOTEST"/current-commit ||
		die "Cannot write current commit's replacement sha1"
	fi

	echo $sha1 > "$DOTEST"/current-commit

	# rewrite parents; if none were rewritten, we can fast-forward.
	new_parents=
	for p in $(git rev-list --parents -1 $sha1 | cut -d' ' -f2-)
	do
		if test -f "$REWRITTEN"/$p
		then
			new_p=$(cat "$REWRITTEN"/$p)
			test $p != $new_p && fast_forward=f
			case "$new_parents" in
			*$new_p*)
				;; # do nothing; that parent is already there
			*)
				new_parents="$new_parents $new_p"
				;;
			esac
		else
			new_parents="$new_parents $p"
		fi
	done
	case $fast_forward in
	t)
		output warn "Fast forward to $sha1"
		output git reset --hard $sha1 ||
			die "Cannot fast forward to $sha1"
		;;
	f)
		test "a$1" = a-n && die "Refusing to squash a merge: $sha1"

		first_parent=$(expr "$new_parents" : ' \([^ ]*\)')
		# detach HEAD to current parent
		output git checkout $first_parent 2> /dev/null ||
			die "Cannot move HEAD to $first_parent"

		case "$new_parents" in
		' '*' '*)
			# redo merge
			author_script=$(get_author_ident_from_commit $sha1)
			eval "$author_script"
			msg="$(git cat-file commit $sha1 | sed -e '1,/^$/d')"
			# No point in merging the first parent, that's HEAD
			new_parents=${new_parents# $first_parent}
			if ! GIT_AUTHOR_NAME="$GIT_AUTHOR_NAME" \
				GIT_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" \
				GIT_AUTHOR_DATE="$GIT_AUTHOR_DATE" \
				output git merge $STRATEGY -m "$msg" \
					$new_parents
			then
				git rerere
				printf "%s\n" "$msg" > "$GIT_DIR"/MERGE_MSG
				die Error redoing merge $sha1
			fi
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

make_squash_message () {
	if test -f "$SQUASH_MSG"; then
		COUNT=$(($(sed -n "s/^# This is [^0-9]*\([1-9][0-9]*\).*/\1/p" \
			< "$SQUASH_MSG" | sed -ne '$p')+1))
		echo "# This is a combination of $COUNT commits."
		sed -e 1d -e '2,/^./{
			/^$/d
		}' <"$SQUASH_MSG"
	else
		COUNT=2
		echo "# This is a combination of two commits."
		echo "# The first commit's message is:"
		echo
		git cat-file commit HEAD | sed -e '1,/^$/d'
	fi
	echo
	echo "# This is the $(nth_string $COUNT) commit message:"
	echo
	git cat-file commit $1 | sed -e '1,/^$/d'
}

peek_next_command () {
	sed -n "1s/ .*$//p" < "$TODO"
}

do_next () {
	rm -f "$DOTEST"/message "$DOTEST"/author-script \
		"$DOTEST"/amend || exit
	read command sha1 rest < "$TODO"
	case "$command" in
	'#'*|'')
		mark_action_done
		;;
	pick|p)
		comment_for_reflog pick

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		;;
	edit|e)
		comment_for_reflog edit

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		make_patch $sha1
		: > "$DOTEST"/amend
		warn "Stopped at $sha1... $rest"
		warn "You can amend the commit now, with"
		warn
		warn "	git commit --amend"
		warn
		warn "Once you are satisfied with your changes, run"
		warn
		warn "	git rebase --continue"
		warn
		exit 0
		;;
	squash|s)
		comment_for_reflog squash

		has_action "$DONE" ||
			die "Cannot 'squash' without a previous commit"

		mark_action_done
		make_squash_message $sha1 > "$MSG"
		case "$(peek_next_command)" in
		squash|s)
			EDIT_COMMIT=
			USE_OUTPUT=output
			cp "$MSG" "$SQUASH_MSG"
			;;
		*)
			EDIT_COMMIT=-e
			USE_OUTPUT=
			rm -f "$SQUASH_MSG" || exit
			;;
		esac

		failed=f
		author_script=$(get_author_ident_from_commit HEAD)
		output git reset --soft HEAD^
		pick_one -n $sha1 || failed=t
		echo "$author_script" > "$DOTEST"/author-script
		if test $failed = f
		then
			# This is like --amend, but with a different message
			eval "$author_script"
			GIT_AUTHOR_NAME="$GIT_AUTHOR_NAME" \
			GIT_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" \
			GIT_AUTHOR_DATE="$GIT_AUTHOR_DATE" \
			$USE_OUTPUT git commit --no-verify -F "$MSG" $EDIT_COMMIT || failed=t
		fi
		if test $failed = t
		then
			cp "$MSG" "$GIT_DIR"/MERGE_MSG
			warn
			warn "Could not apply $sha1... $rest"
			die_with_patch $sha1 ""
		fi
		;;
	*)
		warn "Unknown command: $command $sha1 $rest"
		die_with_patch $sha1 "Please fix this in the file $TODO."
		;;
	esac
	test -s "$TODO" && return

	comment_for_reflog finish &&
	HEADNAME=$(cat "$DOTEST"/head-name) &&
	OLDHEAD=$(cat "$DOTEST"/head) &&
	SHORTONTO=$(git rev-parse --short $(cat "$DOTEST"/onto)) &&
	if test -d "$REWRITTEN"
	then
		test -f "$DOTEST"/current-commit &&
			current_commit=$(cat "$DOTEST"/current-commit) &&
			git rev-parse HEAD > "$REWRITTEN"/$current_commit
		if test -f "$REWRITTEN"/$OLDHEAD
		then
			NEWHEAD=$(cat "$REWRITTEN"/$OLDHEAD)
		else
			NEWHEAD=$OLDHEAD
		fi
	else
		NEWHEAD=$(git rev-parse HEAD)
	fi &&
	case $HEADNAME in
	refs/*)
		message="$GIT_REFLOG_ACTION: $HEADNAME onto $SHORTONTO)" &&
		git update-ref -m "$message" $HEADNAME $NEWHEAD $OLDHEAD &&
		git symbolic-ref HEAD $HEADNAME
		;;
	esac && {
		test ! -f "$DOTEST"/verbose ||
			git diff-tree --stat $(cat "$DOTEST"/head)..HEAD
	} &&
	rm -rf "$DOTEST" &&
	git gc --auto &&
	warn "Successfully rebased and updated $HEADNAME."

	exit
}

do_rest () {
	while :
	do
		do_next
	done
}

# check if no other options are set
is_standalone () {
	test $# -eq 2 -a "$2" = '--' &&
	test -z "$ONTO" &&
	test -z "$PRESERVE_MERGES" &&
	test -z "$STRATEGY" &&
	test -z "$VERBOSE"
}

get_saved_options () {
	test -d "$REWRITTEN" && PRESERVE_MERGES=t
	test -f "$DOTEST"/strategy && STRATEGY="$(cat "$DOTEST"/strategy)"
	test -f "$DOTEST"/verbose && VERBOSE=t
}

while test $# != 0
do
	case "$1" in
	--continue)
		is_standalone "$@" || usage
		get_saved_options
		comment_for_reflog continue

		test -d "$DOTEST" || die "No interactive rebase running"

		# Sanity check
		git rev-parse --verify HEAD >/dev/null ||
			die "Cannot read HEAD"
		git update-index --ignore-submodules --refresh &&
			git diff-files --quiet --ignore-submodules ||
			die "Working tree is dirty"

		# do we have anything to commit?
		if git diff-index --cached --quiet --ignore-submodules HEAD --
		then
			: Nothing to commit -- skip this
		else
			. "$DOTEST"/author-script ||
				die "Cannot find the author identity"
			if test -f "$DOTEST"/amend
			then
				git reset --soft HEAD^ ||
				die "Cannot rewind the HEAD"
			fi
			export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE &&
			git commit --no-verify -F "$DOTEST"/message -e ||
			die "Could not commit staged changes."
		fi

		require_clean_work_tree
		do_rest
		;;
	--abort)
		is_standalone "$@" || usage
		get_saved_options
		comment_for_reflog abort

		git rerere clear
		test -d "$DOTEST" || die "No interactive rebase running"

		HEADNAME=$(cat "$DOTEST"/head-name)
		HEAD=$(cat "$DOTEST"/head)
		case $HEADNAME in
		refs/*)
			git symbolic-ref HEAD $HEADNAME
			;;
		esac &&
		output git reset --hard $HEAD &&
		rm -rf "$DOTEST"
		exit
		;;
	--skip)
		is_standalone "$@" || usage
		get_saved_options
		comment_for_reflog skip

		git rerere clear
		test -d "$DOTEST" || die "No interactive rebase running"

		output git reset --hard && do_rest
		;;
	-s)
		case "$#,$1" in
		*,*=*)
			STRATEGY="-s "$(expr "z$1" : 'z-[^=]*=\(.*\)') ;;
		1,*)
			usage ;;
		*)
			STRATEGY="-s $2"
			shift ;;
		esac
		;;
	-m)
		# we use merge anyway
		;;
	-v)
		VERBOSE=t
		;;
	-p)
		PRESERVE_MERGES=t
		;;
	-i)
		# yeah, we know
		;;
	--onto)
		shift
		ONTO=$(git rev-parse --verify "$1") ||
			die "Does not point to a valid commit: $1"
		;;
	--)
		shift
		test $# -eq 1 -o $# -eq 2 || usage
		test -d "$DOTEST" &&
			die "Interactive rebase already started"

		git var GIT_COMMITTER_IDENT >/dev/null ||
			die "You need to set your committer info first"

		comment_for_reflog start

		require_clean_work_tree

		UPSTREAM=$(git rev-parse --verify "$1") || die "Invalid base"
		test -z "$ONTO" && ONTO=$UPSTREAM

		if test ! -z "$2"
		then
			output git show-ref --verify --quiet "refs/heads/$2" ||
				die "Invalid branchname: $2"
			output git checkout "$2" ||
				die "Could not checkout $2"
		fi

		HEAD=$(git rev-parse --verify HEAD) || die "No HEAD?"
		mkdir "$DOTEST" || die "Could not create temporary $DOTEST"

		: > "$DOTEST"/interactive || die "Could not mark as interactive"
		git symbolic-ref HEAD > "$DOTEST"/head-name 2> /dev/null ||
			echo "detached HEAD" > "$DOTEST"/head-name

		echo $HEAD > "$DOTEST"/head
		echo $UPSTREAM > "$DOTEST"/upstream
		echo $ONTO > "$DOTEST"/onto
		test -z "$STRATEGY" || echo "$STRATEGY" > "$DOTEST"/strategy
		test t = "$VERBOSE" && : > "$DOTEST"/verbose
		if test t = "$PRESERVE_MERGES"
		then
			# $REWRITTEN contains files for each commit that is
			# reachable by at least one merge base of $HEAD and
			# $UPSTREAM. They are not necessarily rewritten, but
			# their children might be.
			# This ensures that commits on merged, but otherwise
			# unrelated side branches are left alone. (Think "X"
			# in the man page's example.)
			mkdir "$REWRITTEN" &&
			for c in $(git merge-base --all $HEAD $UPSTREAM)
			do
				echo $ONTO > "$REWRITTEN"/$c ||
					die "Could not init rewritten commits"
			done
			MERGES_OPTION=
		else
			MERGES_OPTION=--no-merges
		fi

		SHORTUPSTREAM=$(git rev-parse --short $UPSTREAM)
		SHORTHEAD=$(git rev-parse --short $HEAD)
		SHORTONTO=$(git rev-parse --short $ONTO)
		git rev-list $MERGES_OPTION --pretty=oneline --abbrev-commit \
			--abbrev=7 --reverse --left-right --cherry-pick \
			$UPSTREAM...$HEAD | \
			sed -n "s/^>/pick /p" > "$TODO"
		cat >> "$TODO" << EOF

# Rebase $SHORTUPSTREAM..$SHORTHEAD onto $SHORTONTO
#
# Commands:
#  p, pick = use commit
#  e, edit = use commit, but stop for amending
#  s, squash = use commit, but meld into previous commit
#
# If you remove a line here THAT COMMIT WILL BE LOST.
# However, if you remove everything, the rebase will be aborted.
#
EOF

		has_action "$TODO" ||
			die_abort "Nothing to do"

		cp "$TODO" "$TODO".backup
		git_editor "$TODO" ||
			die "Could not execute editor"

		has_action "$TODO" ||
			die_abort "Nothing to do"

		git update-ref ORIG_HEAD $HEAD
		output git checkout $ONTO && do_rest
		;;
	esac
	shift
done
