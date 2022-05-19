#!/bin/sh

# wrap-for-bin.sh: Template for but executable wrapper scripts
# to run test suite against sandbox, but with only bindir-installed
# executables in PATH.  The Makefile copies this into various
# files in bin-wrappers, substituting
# @@BUILD_DIR@@ and @@PROG@@.

BUT_EXEC_PATH='@@BUILD_DIR@@'
if test -n "$NO_SET_BUT_TEMPLATE_DIR"
then
	unset BUT_TEMPLATE_DIR
else
	BUT_TEMPLATE_DIR='@@BUILD_DIR@@/templates/blt'
	export BUT_TEMPLATE_DIR
fi
BUTPERLLIB='@@BUILD_DIR@@/perl/build/lib'"${BUTPERLLIB:+:$BUTPERLLIB}"
BUT_TEXTDOMAINDIR='@@BUILD_DIR@@/po/build/locale'
PATH='@@BUILD_DIR@@/bin-wrappers:'"$PATH"

export BUT_EXEC_PATH BUTPERLLIB PATH BUT_TEXTDOMAINDIR

case "$BUT_DEBUGGER" in
'')
	exec "${BUT_EXEC_PATH}/@@PROG@@" "$@"
	;;
1)
	unset BUT_DEBUGGER
	exec gdb --args "${BUT_EXEC_PATH}/@@PROG@@" "$@"
	;;
*)
	BUT_DEBUGGER_ARGS="$BUT_DEBUGGER"
	unset BUT_DEBUGGER
	exec ${BUT_DEBUGGER_ARGS} "${BUT_EXEC_PATH}/@@PROG@@" "$@"
	;;
esac
