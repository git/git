#!/bin/sh
#
# Set up GIT_DIR and GIT_OBJECT_DIRECTORY
# and return true if everything looks ok
#
: ${GIT_DIR=.git}
: ${GIT_OBJECT_DIRECTORY="$GIT_DIR/objects"}

# Having this variable in your environment would break scripts because
# you would cause "cd" to be be taken to unexpected places.  If you
# like CDPATH, define it for your interactive shell sessions without
# exporting it.
unset CDPATH

die() {
	echo >&2 "$@"
	exit 1
}

case "$(GIT_DIR="$GIT_DIR" git-symbolic-ref HEAD 2>/dev/null)" in
refs/*)	: ;;
*)	false ;;
esac &&
[ -d "$GIT_DIR/refs" ] &&
[ -d "$GIT_OBJECT_DIRECTORY/00" ]
