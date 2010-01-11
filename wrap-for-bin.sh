#!/bin/sh

# wrap-for-bin.sh: Template for git executable wrapper scripts
# to run test suite against sandbox, but with only bindir-installed
# executables in PATH.  The Makefile copies this into various
# files in bin-wrappers, substituting
# @@BUILD_DIR@@ and @@PROG@@.

GIT_EXEC_PATH='@@BUILD_DIR@@'
GIT_TEMPLATE_DIR='@@BUILD_DIR@@/templates/blt'
GITPERLLIB='@@BUILD_DIR@@/perl/blib/lib'
PATH='@@BUILD_DIR@@/bin-wrappers:'"$PATH"
export GIT_EXEC_PATH GIT_TEMPLATE_DIR GITPERLLIB PATH

exec "${GIT_EXEC_PATH}/@@PROG@@" "$@"
