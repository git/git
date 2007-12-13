#!/bin/sh
#
# This is included in commands that either have to be run from the toplevel
# of the repository, or with GIT_DIR environment variable properly.
# If the GIT_DIR does not look like the right correct git-repository,
# it dies.

# Having this variable in your environment would break scripts because
# you would cause "cd" to be taken to unexpected places.  If you
# like CDPATH, define it for your interactive shell sessions without
# exporting it.
unset CDPATH

die() {
	echo >&2 "$@"
	exit 1
}

if test -n "$OPTIONS_SPEC"; then
	usage() {
		exec "$0" -h
	}

	parseopt_extra=
	[ -n "$OPTIONS_KEEPDASHDASH" ] &&
		parseopt_extra="--keep-dashdash"

	eval "$(
		echo "$OPTIONS_SPEC" |
			git rev-parse --parseopt $parseopt_extra -- "$@" ||
		echo exit $?
	)"
else
	usage() {
		die "Usage: $0 $USAGE"
	}

	if [ -z "$LONG_USAGE" ]
	then
		LONG_USAGE="Usage: $0 $USAGE"
	else
		LONG_USAGE="Usage: $0 $USAGE

$LONG_USAGE"
	fi

	case "$1" in
		-h|--h|--he|--hel|--help)
		echo "$LONG_USAGE"
		exit
	esac
fi

set_reflog_action() {
	if [ -z "${GIT_REFLOG_ACTION:+set}" ]
	then
		GIT_REFLOG_ACTION="$*"
		export GIT_REFLOG_ACTION
	fi
}

git_editor() {
	: "${GIT_EDITOR:=$(git config core.editor)}"
	: "${GIT_EDITOR:=${VISUAL:-${EDITOR}}}"
	case "$GIT_EDITOR,$TERM" in
	,dumb)
		echo >&2 "No editor specified in GIT_EDITOR, core.editor, VISUAL,"
		echo >&2 "or EDITOR. Tried to fall back to vi but terminal is dumb."
		echo >&2 "Please set one of these variables to an appropriate"
		echo >&2 "editor or run $0 with options that will not cause an"
		echo >&2 "editor to be invoked (e.g., -m or -F for git-commit)."
		exit 1
		;;
	esac
	eval "${GIT_EDITOR:=vi}" '"$@"'
}

is_bare_repository () {
	git rev-parse --is-bare-repository
}

cd_to_toplevel () {
	cdup=$(git rev-parse --show-cdup)
	if test ! -z "$cdup"
	then
		cd "$cdup" || {
			echo >&2 "Cannot chdir to $cdup, the toplevel of the working tree"
			exit 1
		}
	fi
}

require_work_tree () {
	test $(git rev-parse --is-inside-work-tree) = true ||
	die "fatal: $0 cannot be used without a working tree."
}

get_author_ident_from_commit () {
	pick_author_script='
	/^author /{
		s/'\''/'\''\\'\'\''/g
		h
		s/^author \([^<]*\) <[^>]*> .*$/\1/
		s/'\''/'\''\'\'\''/g
		s/.*/GIT_AUTHOR_NAME='\''&'\''/p

		g
		s/^author [^<]* <\([^>]*\)> .*$/\1/
		s/'\''/'\''\'\'\''/g
		s/.*/GIT_AUTHOR_EMAIL='\''&'\''/p

		g
		s/^author [^<]* <[^>]*> \(.*\)$/\1/
		s/'\''/'\''\'\'\''/g
		s/.*/GIT_AUTHOR_DATE='\''&'\''/p

		q
	}
	'
	encoding=$(git config i18n.commitencoding || echo UTF-8)
	git show -s --pretty=raw --encoding="$encoding" "$1" |
	LANG=C LC_ALL=C sed -ne "$pick_author_script"
}

# Make sure we are in a valid repository of a vintage we understand,
# if we require to be in a git repository.
if test -n "$NONGIT_OK"
then
	if git rev-parse --git-dir >/dev/null 2>&1
	then
		: ${GIT_DIR=.git}
	fi
else
	if [ -z "$SUBDIRECTORY_OK" ]
	then
		: ${GIT_DIR=.git}
		test -z "$(git rev-parse --show-cdup)" || {
			exit=$?
			echo >&2 "You need to run this command from the toplevel of the working tree."
			exit $exit
		}
	else
		GIT_DIR=$(git rev-parse --git-dir) || {
		    exit=$?
		    echo >&2 "Failed to find a valid git directory."
		    exit $exit
		}
	fi
	test -n "$GIT_DIR" && GIT_DIR=$(cd "$GIT_DIR" && pwd) || {
		echo >&2 "Unable to determine absolute path of git directory"
		exit 1
	}
	: ${GIT_OBJECT_DIRECTORY="$GIT_DIR/objects"}
fi
