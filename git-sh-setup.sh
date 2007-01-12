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
	git-repo-config --bool --get core.bare ||
	case "$GIT_DIR" in
	.git | */.git) echo false ;;
	*) echo true ;;
	esac
}

require_work_tree () {
	test $(is_bare_repository) = false ||
	die "fatal: $0 cannot be used without a working tree."
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
