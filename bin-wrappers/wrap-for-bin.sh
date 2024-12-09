#!/bin/sh

# wrap-for-bin.sh: Template for git executable wrapper scripts
# to run test suite against sandbox, but with only bindir-installed
# executables in PATH.  The Makefile copies this into various
# files in bin-wrappers, substituting
# @BUILD_DIR@, @TEMPLATE_DIR@ and @PROG@.

GIT_EXEC_PATH='@BUILD_DIR@'
if test -n "$NO_SET_GIT_TEMPLATE_DIR"
then
	unset GIT_TEMPLATE_DIR
else
	GIT_TEMPLATE_DIR='@TEMPLATE_DIR@'
	export GIT_TEMPLATE_DIR
fi
MERGE_TOOLS_DIR='@MERGE_TOOLS_DIR@'
GITPERLLIB='@GITPERLLIB@'"${GITPERLLIB:+:$GITPERLLIB}"
GIT_TEXTDOMAINDIR='@GIT_TEXTDOMAINDIR@'
PATH='@BUILD_DIR@/bin-wrappers:'"$PATH"

export MERGE_TOOLS_DIR GIT_EXEC_PATH GITPERLLIB PATH GIT_TEXTDOMAINDIR

case "$GIT_DEBUGGER" in
'')
	exec "@PROG@" "$@"
	;;
1)
	unset GIT_DEBUGGER
	exec gdb --args "@PROG@" "$@"
	;;
*)
	GIT_DEBUGGER_ARGS="$GIT_DEBUGGER"
	unset GIT_DEBUGGER
	exec ${GIT_DEBUGGER_ARGS} "@PROG@" "$@"
	;;
esac
