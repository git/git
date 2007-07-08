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

. git-sh-setup
require_work_tree

DOTEST="$GIT_DIR/.dotest-merge"
TODO="$DOTEST"/todo
DONE="$DOTEST"/done
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
	esac
}

mark_action_done () {
	sed -e 1q < "$TODO" >> "$DONE"
	sed -e 1d < "$TODO" >> "$TODO".new
	mv -f "$TODO".new "$TODO"
}

make_patch () {
	parent_sha1=$(git rev-parse --verify "$1"^ 2> /dev/null)
	git diff "$parent_sha1".."$1" > "$DOTEST"/patch
}

die_with_patch () {
	test -f "$DOTEST"/message ||
		git cat-file commit $sha1 | sed "1,/^$/d" > "$DOTEST"/message
	test -f "$DOTEST"/author-script ||
		get_author_ident_from_commit $sha1 > "$DOTEST"/author-script
	make_patch "$1"
	die "$2"
}

die_abort () {
	rm -rf "$DOTEST"
	die "$1"
}

pick_one () {
	case "$1" in -n) sha1=$2 ;; *) sha1=$1 ;; esac
	git rev-parse --verify $sha1 || die "Invalid commit name: $sha1"
	test -d "$REWRITTEN" &&
		pick_one_preserving_merges "$@" && return
	parent_sha1=$(git rev-parse --verify $sha1^ 2>/dev/null)
	current_sha1=$(git rev-parse --verify HEAD)
	if [ $current_sha1 = $parent_sha1 ]; then
		git reset --hard $sha1
		sha1=$(git rev-parse --short $sha1)
		warn Fast forward to $sha1
	else
		git cherry-pick $STRATEGY "$@"
	fi
}

pick_one_preserving_merges () {
	case "$1" in -n) sha1=$2 ;; *) sha1=$1 ;; esac
	sha1=$(git rev-parse $sha1)

	if [ -f "$DOTEST"/current-commit ]
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
	for p in $(git rev-list --parents -1 $sha1 | cut -d\  -f2-)
	do
		if [ -f "$REWRITTEN"/$p ]
		then
			preserve=f
			new_p=$(cat "$REWRITTEN"/$p)
			test $p != $new_p && fast_forward=f
			case "$new_parents" in
			*$new_p*)
				;; # do nothing; that parent is already there
			*)
				new_parents="$new_parents $new_p"
			esac
		fi
	done
	case $fast_forward in
	t)
		echo "Fast forward to $sha1"
		test $preserve=f && echo $sha1 > "$REWRITTEN"/$sha1
		;;
	f)
		test "a$1" = a-n && die "Refusing to squash a merge: $sha1"

		first_parent=$(expr "$new_parents" : " \([^ ]*\)")
		# detach HEAD to current parent
		git checkout $first_parent 2> /dev/null ||
			die "Cannot move HEAD to $first_parent"

		echo $sha1 > "$DOTEST"/current-commit
		case "$new_parents" in
		\ *\ *)
			# redo merge
			author_script=$(get_author_ident_from_commit $sha1)
			eval "$author_script"
			msg="$(git cat-file commit $sha1 | \
				sed -e '1,/^$/d' -e "s/[\"\\]/\\\\&/g")"
			# NEEDSWORK: give rerere a chance
			if ! git merge $STRATEGY -m "$msg" $new_parents
			then
				echo "$msg" > "$GIT_DIR"/MERGE_MSG
				die Error redoing merge $sha1
			fi
			;;
		*)
			git cherry-pick $STRATEGY "$@" ||
				die_with_patch $sha1 "Could not pick $sha1"
		esac
	esac
}

do_next () {
	test -f "$DOTEST"/message && rm "$DOTEST"/message
	test -f "$DOTEST"/author-script && rm "$DOTEST"/author-script
	read command sha1 rest < "$TODO"
	case "$command" in
	\#|'')
		mark_action_done
		;;
	pick)
		comment_for_reflog pick

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		;;
	edit)
		comment_for_reflog edit

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		make_patch $sha1
		warn
		warn "You can amend the commit now, with"
		warn
		warn "	git commit --amend"
		warn
		exit 0
		;;
	squash)
		comment_for_reflog squash

		test -z "$(grep -ve '^$' -e '^#' < $DONE)" &&
			die "Cannot 'squash' without a previous commit"

		mark_action_done
		failed=f
		pick_one -n $sha1 || failed=t
		MSG="$DOTEST"/message
		echo "# This is a combination of two commits." > "$MSG"
		echo "# The first commit's message is:" >> "$MSG"
		echo >> "$MSG"
		git cat-file commit HEAD | sed -e '1,/^$/d' >> "$MSG"
		echo >> "$MSG"
		echo "# And this is the 2nd commit message:" >> "$MSG"
		echo >> "$MSG"
		git cat-file commit $sha1 | sed -e '1,/^$/d' >> "$MSG"
		git reset --soft HEAD^
		author_script=$(get_author_ident_from_commit $sha1)
		echo "$author_script" > "$DOTEST"/author-script
		case $failed in
		f)
			# This is like --amend, but with a different message
			eval "$author_script"
			export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE
			git commit -F "$MSG" -e
			;;
		t)
			cp "$MSG" "$GIT_DIR"/MERGE_MSG
			warn
			warn "Could not apply $sha1... $rest"
			die_with_patch $sha1 ""
		esac
		;;
	*)
		warn "Unknown command: $command $sha1 $rest"
		die_with_patch $sha1 "Please fix this in the file $TODO."
	esac
	test -s "$TODO" && return

	comment_for_reflog finish &&
	HEADNAME=$(cat "$DOTEST"/head-name) &&
	OLDHEAD=$(cat "$DOTEST"/head) &&
	SHORTONTO=$(git rev-parse --short $(cat "$DOTEST"/onto)) &&
	if [ -d "$REWRITTEN" ]
	then
		test -f "$DOTEST"/current-commit &&
			current_commit=$(cat "$DOTEST"/current-commit) &&
			git rev-parse HEAD > "$REWRITTEN"/$current_commit
		NEWHEAD=$(cat "$REWRITTEN"/$OLDHEAD)
	else
		NEWHEAD=$(git rev-parse HEAD)
	fi &&
	message="$GIT_REFLOG_ACTION: $HEADNAME onto $SHORTONTO)" &&
	git update-ref -m "$message" $HEADNAME $NEWHEAD $OLDHEAD &&
	git symbolic-ref HEAD $HEADNAME && {
		test ! -f "$DOTEST"/verbose ||
			git diff --stat $(cat "$DOTEST"/head)..HEAD
	} &&
	rm -rf "$DOTEST" &&
	warn "Successfully rebased and updated $HEADNAME."

	exit
}

do_rest () {
	while :
	do
		do_next
	done
}

while case $# in 0) break ;; esac
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
		. "$DOTEST"/author-script &&
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
		git symbolic-ref HEAD $HEADNAME &&
		git reset --hard $HEAD &&
		rm -rf "$DOTEST"
		exit
		;;
	--skip)
		comment_for_reflog skip

		test -d "$DOTEST" || die "No interactive rebase running"

		git reset --hard && do_rest
		;;
	-s|--strategy)
		shift
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

		if [ ! -z "$2"]
		then
			git show-ref --verify --quiet "refs/heads/$2" ||
				die "Invalid branchname: $2"
			git checkout "$2" ||
				die "Could not checkout $2"
		fi

		HEAD=$(git rev-parse --verify HEAD) || die "No HEAD?"
		UPSTREAM=$(git rev-parse --verify "$1") || die "Invalid base"

		test -z "$ONTO" && ONTO=$UPSTREAM

		mkdir "$DOTEST" || die "Could not create temporary $DOTEST"
		: > "$DOTEST"/interactive || die "Could not mark as interactive"
		git symbolic-ref HEAD > "$DOTEST"/head-name ||
			die "Could not get HEAD"

		echo $HEAD > "$DOTEST"/head
		echo $UPSTREAM > "$DOTEST"/upstream
		echo $ONTO > "$DOTEST"/onto
		test -z "$STRATEGY" || echo "$STRATEGY" > "$DOTEST"/strategy
		test t = "$VERBOSE" && : > "$DOTEST"/verbose
		if [ t = "$PRESERVE_MERGES" ]
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
			--abbrev=7 --reverse $UPSTREAM..$HEAD | \
			sed "s/^/pick /" >> "$TODO"

		test -z "$(grep -ve '^$' -e '^#' < $TODO)" &&
			die_abort "Nothing to do"

		cp "$TODO" "$TODO".backup
		${VISUAL:-${EDITOR:-vi}} "$TODO" ||
			die "Could not execute editor"

		test -z "$(grep -ve '^$' -e '^#' < $TODO)" &&
			die_abort "Nothing to do"

		git checkout $ONTO && do_rest
	esac
	shift
done
