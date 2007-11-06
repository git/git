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

USAGE='(--continue | --abort | --skip | [--preserve-merges] [--verbose]
	[--onto <branch>] <upstream> [<branch>])'

OPTIONS_SPEC=
. git-sh-setup
require_work_tree

DOTEST="$GIT_DIR/.dotest-merge"
TODO="$DOTEST"/git-rebase-todo
DONE="$DOTEST"/done
MSG="$DOTEST"/message
SQUASH_MSG="$DOTEST"/message-squash
REWRITTEN="$DOTEST"/rewritten
PRESERVE_MERGES=
STRATEGY=
VERBOSE=
test -d "$REWRITTEN" && PRESERVE_MERGES=t
test -f "$DOTEST"/strategy && STRATEGY="$(cat "$DOTEST"/strategy)"
test -f "$DOTEST"/verbose && VERBOSE=t

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
	git update-index --refresh &&
	git diff-files --quiet &&
	git diff-index --cached --quiet HEAD ||
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

mark_action_done () {
	sed -e 1q < "$TODO" >> "$DONE"
	sed -e 1d < "$TODO" >> "$TODO".new
	mv -f "$TODO".new "$TODO"
	count=$(($(grep -ve '^$' -e '^#' < "$DONE" | wc -l)))
	total=$(($count+$(grep -ve '^$' -e '^#' < "$TODO" | wc -l)))
	printf "Rebasing (%d/%d)\r" $count $total
	test -z "$VERBOSE" || echo
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
	die "$2"
}

die_abort () {
	rm -rf "$DOTEST"
	die "$1"
}

has_action () {
	grep -vqe '^$' -e '^#' "$1"
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
	case "$1" in -n) sha1=$2 ;; *) sha1=$1 ;; esac
	sha1=$(git rev-parse $sha1)

	if test -f "$DOTEST"/current-commit
	then
		current_commit=$(cat "$DOTEST"/current-commit) &&
		git rev-parse HEAD > "$REWRITTEN"/$current_commit &&
		rm "$DOTEST"/current-commit ||
		die "Cannot write current commit's replacement sha1"
	fi

	# rewrite parents; if none were rewritten, we can fast-forward.
	fast_forward=t
	preserve=t
	new_parents=
	for p in $(git rev-list --parents -1 $sha1 | cut -d' ' -f2-)
	do
		if test -f "$REWRITTEN"/$p
		then
			preserve=f
			new_p=$(cat "$REWRITTEN"/$p)
			test $p != $new_p && fast_forward=f
			case "$new_parents" in
			*$new_p*)
				;; # do nothing; that parent is already there
			*)
				new_parents="$new_parents $new_p"
				;;
			esac
		fi
	done
	case $fast_forward in
	t)
		output warn "Fast forward to $sha1"
		test $preserve = f || echo $sha1 > "$REWRITTEN"/$sha1
		;;
	f)
		test "a$1" = a-n && die "Refusing to squash a merge: $sha1"

		first_parent=$(expr "$new_parents" : ' \([^ ]*\)')
		# detach HEAD to current parent
		output git checkout $first_parent 2> /dev/null ||
			die "Cannot move HEAD to $first_parent"

		echo $sha1 > "$DOTEST"/current-commit
		case "$new_parents" in
		' '*' '*)
			# redo merge
			author_script=$(get_author_ident_from_commit $sha1)
			eval "$author_script"
			msg="$(git cat-file commit $sha1 | sed -e '1,/^$/d')"
			# No point in merging the first parent, that's HEAD
			new_parents=${new_parents# $first_parent}
			# NEEDSWORK: give rerere a chance
			if ! GIT_AUTHOR_NAME="$GIT_AUTHOR_NAME" \
				GIT_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" \
				GIT_AUTHOR_DATE="$GIT_AUTHOR_DATE" \
				output git merge $STRATEGY -m "$msg" \
					$new_parents
			then
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
			< "$SQUASH_MSG" | tail -n 1)+1))
		echo "# This is a combination of $COUNT commits."
		sed -n "2,\$p" < "$SQUASH_MSG"
	else
		COUNT=2
		echo "# This is a combination of two commits."
		echo "# The first commit's message is:"
		echo
		git cat-file commit HEAD | sed -e '1,/^$/d'
		echo
	fi
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
		warn
		warn "You can amend the commit now, with"
		warn
		warn "	git commit --amend"
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
		case $failed in
		f)
			# This is like --amend, but with a different message
			eval "$author_script"
			GIT_AUTHOR_NAME="$GIT_AUTHOR_NAME" \
			GIT_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" \
			GIT_AUTHOR_DATE="$GIT_AUTHOR_DATE" \
			$USE_OUTPUT git commit -F "$MSG" $EDIT_COMMIT
			;;
		t)
			cp "$MSG" "$GIT_DIR"/MERGE_MSG
			warn
			warn "Could not apply $sha1... $rest"
			die_with_patch $sha1 ""
			;;
		esac
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
		NEWHEAD=$(cat "$REWRITTEN"/$OLDHEAD)
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

while test $# != 0
do
	case "$1" in
	--continue)
		comment_for_reflog continue

		test -d "$DOTEST" || die "No interactive rebase running"

		# commit if necessary
		git rev-parse --verify HEAD > /dev/null &&
		git update-index --refresh &&
		git diff-files --quiet &&
		! git diff-index --cached --quiet HEAD &&
		. "$DOTEST"/author-script && {
			test ! -f "$DOTEST"/amend || git reset --soft HEAD^
		} &&
		export GIT_AUTHOR_NAME GIT_AUTHOR_NAME GIT_AUTHOR_DATE &&
		git commit -F "$DOTEST"/message -e

		require_clean_work_tree
		do_rest
		;;
	--abort)
		comment_for_reflog abort

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
		comment_for_reflog skip

		test -d "$DOTEST" || die "No interactive rebase running"

		output git reset --hard && do_rest
		;;
	-s|--strategy)
		case "$#,$1" in
		*,*=*)
			STRATEGY="-s `expr "z$1" : 'z-[^=]*=\(.*\)'`" ;;
		1,*)
			usage ;;
		*)
			STRATEGY="-s $2"
			shift ;;
		esac
		;;
	--merge)
		# we use merge anyway
		;;
	-C*)
		die "Interactive rebase uses merge, so $1 does not make sense"
		;;
	-v|--verbose)
		VERBOSE=t
		;;
	-p|--preserve-merges)
		PRESERVE_MERGES=t
		;;
	-i|--interactive)
		# yeah, we know
		;;
	''|-h)
		usage
		;;
	*)
		test -d "$DOTEST" &&
			die "Interactive rebase already started"

		git var GIT_COMMITTER_IDENT >/dev/null ||
			die "You need to set your committer info first"

		comment_for_reflog start

		ONTO=
		case "$1" in
		--onto)
			ONTO=$(git rev-parse --verify "$2") ||
				die "Does not point to a valid commit: $2"
			shift; shift
			;;
		esac

		require_clean_work_tree

		if test ! -z "$2"
		then
			output git show-ref --verify --quiet "refs/heads/$2" ||
				die "Invalid branchname: $2"
			output git checkout "$2" ||
				die "Could not checkout $2"
		fi

		HEAD=$(git rev-parse --verify HEAD) || die "No HEAD?"
		UPSTREAM=$(git rev-parse --verify "$1") || die "Invalid base"

		mkdir "$DOTEST" || die "Could not create temporary $DOTEST"

		test -z "$ONTO" && ONTO=$UPSTREAM

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
		cat > "$TODO" << EOF
# Rebasing $SHORTUPSTREAM..$SHORTHEAD onto $SHORTONTO
#
# Commands:
#  pick = use commit
#  edit = use commit, but stop for amending
#  squash = use commit, but meld into previous commit
#
# If you remove a line here THAT COMMIT WILL BE LOST.
#
EOF
		git rev-list $MERGES_OPTION --pretty=oneline --abbrev-commit \
			--abbrev=7 --reverse --left-right --cherry-pick \
			$UPSTREAM...$HEAD | \
			sed -n "s/^>/pick /p" >> "$TODO"

		has_action "$TODO" ||
			die_abort "Nothing to do"

		cp "$TODO" "$TODO".backup
		git_editor "$TODO" ||
			die "Could not execute editor"

		has_action "$TODO" ||
			die_abort "Nothing to do"

		output git checkout $ONTO && do_rest
		;;
	esac
	shift
done
