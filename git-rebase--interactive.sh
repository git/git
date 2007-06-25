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

USAGE='(--continue | --abort | --skip | [--onto <branch>] <upstream> [<branch>])'

. git-sh-setup
require_work_tree

DOTEST="$GIT_DIR/.dotest-merge"
TODO="$DOTEST"/todo
DONE="$DOTEST"/done
STRATEGY=
VERBOSE=

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

do_next () {
	read command sha1 rest < "$TODO"
	case "$command" in
	\#|'')
		mark_action_done
		continue
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
			warn "After you fixed that, commit the result with"
			warn
			warn "  $(echo $author_script | tr '\012' ' ') \\"
			warn "	  git commit -F \"$GIT_DIR\"/MERGE_MSG -e"
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
	NEWHEAD=$(git rev-parse HEAD) &&
	message="$GIT_REFLOG_ACTION: $HEADNAME onto $SHORTONTO)" &&
	git update-ref -m "$message" $HEADNAME $NEWHEAD $OLDHEAD &&
	git symbolic-ref HEAD $HEADNAME &&
	rm -rf "$DOTEST" &&
	warn "Successfully rebased and updated $HEADNAME."

	exit
}

do_rest () {
	while :
	do
		do_next
	done
	test -f "$DOTEST"/verbose &&
		git diff --stat $(cat "$DOTEST"/head)..HEAD
	exit
}

while case $# in 0) break ;; esac
do
	case "$1" in
	--continue)
		comment_for_reflog continue

		test -d "$DOTEST" || die "No interactive rebase running"

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
		test t = "$VERBOSE" && : > "$DOTEST"/verbose

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
EOF
		git rev-list --no-merges --pretty=oneline --abbrev-commit \
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
