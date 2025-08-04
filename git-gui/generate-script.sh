#!/bin/sh

set -e

if test $# -ne 3
then
	echo >&2 "USAGE: $0 <OUTPUT> <INPUT> <GIT-GUI-BUILD-OPTIONS>"
	exit 1
fi

OUTPUT="$1"
INPUT="$2"
BUILD_OPTIONS="$3"

. "$BUILD_OPTIONS"

sed \
	-e "1s|#!.*/sh|#!$SHELL_PATH|" \
	-e "1,3s|^exec wish|exec '$TCLTK_PATH'|" \
	"$INPUT" >"$OUTPUT"

chmod a+x "$OUTPUT"
