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

# Make sure we are in a valid repository of a vintage we understand.
GIT_DIR="$GIT_DIR" git-var GIT_AUTHOR_IDENT >/dev/null || exit
