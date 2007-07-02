#!/bin/sh
#
# This is included in commands that either have to be run from the toplevel
# of the repository, or with GIT_DIR environment variable properly.
# If the GIT_DIR does not look like the right correct git-repository,
# it dies.

# Having this variable in your environment would break scripts because
# you would cause "cd" to be be taken to unexpected places.  If you
# like CDPATH, define it for your interactive shell sessions without
# exporting it.
unset CDPATH

die() {
	echo >&2 "$@"
	exit 1
}

usage() {
	die "Usage: $0 $USAGE"
}

set_reflog_action() {
	if [ -z "${GIT_REFLOG_ACTION:+set}" ]
	then
		GIT_REFLOG_ACTION="$*"
		export GIT_REFLOG_ACTION
	fi
}

is_bare_repository () {
	git-rev-parse --is-bare-repository
}

cd_to_toplevel () {
	cdup=$(git-rev-parse --show-cdup)
	if test ! -z "$cdup"
	then
		cd "$cdup" || {
			echo >&2 "Cannot chdir to $cdup, the toplevel of the working tree"
			exit 1
		}
	fi
}

require_work_tree () {
	test $(git-rev-parse --is-inside-work-tree) = true &&
	test $(git-rev-parse --is-inside-git-dir) = false ||
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

# Make sure we are in a valid repository of a vintage we understand.
if [ -z "$SUBDIRECTORY_OK" ]
then
	: ${GIT_DIR=.git}
	GIT_DIR=$(GIT_DIR="$GIT_DIR" git-rev-parse --git-dir) || {
		exit=$?
		echo >&2 "You need to run this command from the toplevel of the working tree."
		exit $exit
	}
else
	GIT_DIR=$(git-rev-parse --git-dir) || exit
fi
: ${GIT_OBJECT_DIRECTORY="$GIT_DIR/objects"}
