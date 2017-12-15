#!/bin/sh

# wrap-for-bin.sh: Template for git executable wrapper scripts
# to run test suite against sandbox, but with only bindir-installed
# executables in PATH.  The Makefile copies this into various
# files in bin-wrappers, substituting
# @@BUILD_DIR@@ and @@PROG@@.

GIT_EXEC_PATH='@@BUILD_DIR@@'
if test -n "$NO_SET_GIT_TEMPLATE_DIR"
then
	unset GIT_TEMPLATE_DIR
else
	GIT_TEMPLATE_DIR='@@BUILD_DIR@@/templates/blt'
	export GIT_TEMPLATE_DIR
fi
GITPERLLIB='@@BUILD_DIR@@/perl/build/lib'"${GITPERLLIB:+:$GITPERLLIB}"
GIT_TEXTDOMAINDIR='@@BUILD_DIR@@/po/build/locale'
PATH='@@BUILD_DIR@@/bin-wrappers:'"$PATH"

export GIT_EXEC_PATH GITPERLLIB PATH GIT_TEXTDOMAINDIR

if test -n "$GIT_TEST_GDB"
then
	unset GIT_TEST_GDB
	exec gdb --args "${GIT_EXEC_PATH}/@@PROG@@" "$@"
else
	exec "${GIT_EXEC_PATH}/@@PROG@@" "$@"
fi
