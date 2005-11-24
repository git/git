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

: ${GIT_DIR=.git}
: ${GIT_OBJECT_DIRECTORY="$GIT_DIR/objects"}

die() {
	echo >&2 "$@"
	exit 1
}

case "$(GIT_DIR="$GIT_DIR" git-symbolic-ref HEAD 2>/dev/null)" in
refs/*)	: ;;
*)	false ;;
esac &&
[ -d "$GIT_DIR/refs" ] &&
[ -d "$GIT_OBJECT_DIRECTORY/" ] ||
	die "Not a git repository."
