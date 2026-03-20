#!/bin/sh

set -e

if test $# -ne 3
then
	echo >&2 "USAGE: $0 <GIT_BUILD_OPTIONS> <INPUT> <OUTPUT>"
	exit 1
fi

GIT_BUILD_OPTIONS="$1"
INPUT="$2"
OUTPUT="$3"

. "$GIT_BUILD_OPTIONS"

sed -e "1s|#!.*python|#!$PYTHON_PATH|" \
    "$INPUT" >"$OUTPUT+"
chmod a+x "$OUTPUT+"
mv "$OUTPUT+" "$OUTPUT"
