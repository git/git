#!/bin/sh
#
# Set up GIT_DIR and GIT_OBJECT_DIRECTORY
# and return true if everything looks ok
#
: ${GIT_DIR=.git}
: ${GIT_OBJECT_DIRECTORY="$GIT_DIR/objects"}

die() {
	echo "$@" >&2
	exit 1
}

check_clean_tree() {
    dirty1_=`git-update-index -q --refresh` && {
    dirty2_=`git-diff-index --name-only --cached HEAD`
    case "$dirty2_" in '') : ;; *) (exit 1) ;; esac
    } || {
	echo >&2 "$dirty1_"
	echo "$dirty2_" | sed >&2 -e 's/^/modified: /'
	(exit 1)
    }
}

[ -h "$GIT_DIR/HEAD" ] &&
[ -d "$GIT_DIR/refs" ] &&
[ -d "$GIT_OBJECT_DIRECTORY/00" ]
