#!/bin/sh

set -e

if test "$#" -ne 4
then
	echo >&2 "usage: $0 <INPUT> <OUTPUT> <BUILD_OPTIONS> <VERSION_FILE>"
	exit 1
fi

INPUT="$1"
OUTPUT="$2"
BUILD_OPTIONS="$3"
VERSION_FILE="$4"

. "${BUILD_OPTIONS}"
. "${VERSION_FILE}"

rm -f "$OUTPUT" "$OUTPUT+"
sed \
	-e "1s|#!.*/sh|#!$SHELL_PATH|" \
	-e "s|@@SHELL_PATH@@|$SHELL_PATH|" \
	-e "1,30s|^ exec wish | exec '$TCLTK_PATH' |" \
	-e "s|@@GITGUI_VERSION@@|$GITGUI_VERSION|g" \
	-e "s|@@GITGUI_RELATIVE@@|$GITGUI_RELATIVE|" \
	-e "${GITGUI_RELATIVE}s|@@GITGUI_LIBDIR@@|$GITGUI_LIBDIR|" \
	"$INPUT" >"$OUTPUT"+
chmod +x "$OUTPUT"+
mv "$OUTPUT"+ "$OUTPUT"
