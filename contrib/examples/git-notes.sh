#!/bin/sh

USAGE="(edit [-F <file> | -m <msg>] | show) [commit]"
. git-sh-setup

test -z "$1" && usage
ACTION="$1"; shift

test -z "$GIT_NOTES_REF" && GIT_NOTES_REF="$(git config core.notesref)"
test -z "$GIT_NOTES_REF" && GIT_NOTES_REF="refs/notes/commits"

MESSAGE=
while test $# != 0
do
	case "$1" in
	-m)
		test "$ACTION" = "edit" || usage
		shift
		if test "$#" = "0"; then
			die "error: option -m needs an argument"
		else
			if [ -z "$MESSAGE" ]; then
				MESSAGE="$1"
			else
				MESSAGE="$MESSAGE

$1"
			fi
			shift
		fi
		;;
	-F)
		test "$ACTION" = "edit" || usage
		shift
		if test "$#" = "0"; then
			die "error: option -F needs an argument"
		else
			if [ -z "$MESSAGE" ]; then
				MESSAGE="$(cat "$1")"
			else
				MESSAGE="$MESSAGE

$(cat "$1")"
			fi
			shift
		fi
		;;
	-*)
		usage
		;;
	*)
		break
		;;
	esac
done

COMMIT=$(git rev-parse --verify --default HEAD "$@") ||
die "Invalid commit: $@"

case "$ACTION" in
edit)
	if [ "${GIT_NOTES_REF#refs/notes/}" = "$GIT_NOTES_REF" ]; then
		die "Refusing to edit notes in $GIT_NOTES_REF (outside of refs/notes/)"
	fi

	MSG_FILE="$GIT_DIR/new-notes-$COMMIT"
	GIT_INDEX_FILE="$MSG_FILE.idx"
	export GIT_INDEX_FILE

	trap '
		test -f "$MSG_FILE" && rm "$MSG_FILE"
		test -f "$GIT_INDEX_FILE" && rm "$GIT_INDEX_FILE"
	' 0

	CURRENT_HEAD=$(git show-ref "$GIT_NOTES_REF" | cut -f 1 -d ' ')
	if [ -z "$CURRENT_HEAD" ]; then
		PARENT=
	else
		PARENT="-p $CURRENT_HEAD"
		git read-tree "$GIT_NOTES_REF" || die "Could not read index"
	fi

	if [ -z "$MESSAGE" ]; then
		GIT_NOTES_REF= git log -1 $COMMIT | sed "s/^/#/" > "$MSG_FILE"
		if [ ! -z "$CURRENT_HEAD" ]; then
			git cat-file blob :$COMMIT >> "$MSG_FILE" 2> /dev/null
		fi
		core_editor="$(git config core.editor)"
		${GIT_EDITOR:-${core_editor:-${VISUAL:-${EDITOR:-vi}}}} "$MSG_FILE"
	else
		echo "$MESSAGE" > "$MSG_FILE"
	fi

	grep -v ^# < "$MSG_FILE" | git stripspace > "$MSG_FILE".processed
	mv "$MSG_FILE".processed "$MSG_FILE"
	if [ -s "$MSG_FILE" ]; then
		BLOB=$(git hash-object -w "$MSG_FILE") ||
			die "Could not write into object database"
		git update-index --add --cacheinfo 0644 $BLOB $COMMIT ||
			die "Could not write index"
	else
		test -z "$CURRENT_HEAD" &&
			die "Will not initialise with empty tree"
		git update-index --force-remove $COMMIT ||
			die "Could not update index"
	fi

	TREE=$(git write-tree) || die "Could not write tree"
	NEW_HEAD=$(echo Annotate $COMMIT | git commit-tree $TREE $PARENT) ||
		die "Could not annotate"
	git update-ref -m "Annotate $COMMIT" \
		"$GIT_NOTES_REF" $NEW_HEAD $CURRENT_HEAD
;;
show)
	git rev-parse -q --verify "$GIT_NOTES_REF":$COMMIT > /dev/null ||
		die "No note for commit $COMMIT."
	git show "$GIT_NOTES_REF":$COMMIT
;;
*)
	usage
esac
