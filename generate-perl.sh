#!/bin/sh

set -e

if test $# -ne 5
then
	echo >&2 "USAGE: $0 <GIT_BUILD_OPTIONS> <GIT_VERSION_FILE> <PERL_HEADER> <INPUT> <OUTPUT>"
	exit 1
fi

GIT_BUILD_OPTIONS="$1"
GIT_VERSION_FILE="$2"
PERL_HEADER="$3"
INPUT="$4"
OUTPUT="$5"

. "$GIT_BUILD_OPTIONS"
. "$GIT_VERSION_FILE"

sed -e '1{' \
    -e "	s|#!.*perl|#!$PERL_PATH|" \
    -e "	r $PERL_HEADER" \
    -e '	G' \
    -e '}' \
    -e "s/@GIT_VERSION@/$GIT_VERSION/g" \
    "$INPUT" >"$OUTPUT"
chmod a+x "$OUTPUT"
