#!/bin/sh

set -e

if test $# -ne 3
then
	echo >&2 "USAGE: $0 <INPUT> <OUTPUT> <GIT-BUILD-OPTIONS>"
	exit 1
fi

INPUT="$1"
OUTPUT="$2"
BUILD_OPTIONS="$3"

. "$BUILD_OPTIONS"

sed -e "1s|#!.*/sh|#!$SHELL_PATH|" \
    -e "s|@SHELL_PATH@|$SHELL_PATH|" \
    -e "s|@DIFF@|$DIFF|" \
    -e "s|@LOCALEDIR@|$LOCALEDIR|g" \
    -e "s/@USE_GETTEXT_SCHEME@/$USE_GETTEXT_SCHEME/g" \
    -e "$BROKEN_PATH_FIX" \
    -e "s|@GITWEBDIR@|$GITWEBDIR|g" \
    -e "s|@PERL_PATH@|$PERL_PATH|g" \
    -e "s|@PAGER_ENV@|$PAGER_ENV|g" \
    "$INPUT" >"$OUTPUT"

case "$(basename "$INPUT")" in
git-mergetool--lib.sh|git-sh-i18n.sh|git-sh-setup.sh)
	;;
*)
	chmod a+x "$OUTPUT"
	;;
esac
