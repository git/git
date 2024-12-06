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
    -e "	/^#!.*perl/!b" \
    -e "	s|#!.*perl|#!$PERL_PATH|" \
    -e "	r $PERL_HEADER" \
    -e '	G' \
    -e '}' \
    -e "s|@GIT_VERSION@|$GIT_VERSION|g" \
    -e "s|@LOCALEDIR@|$PERL_LOCALEDIR|g" \
    -e "s|@NO_GETTEXT@|$NO_GETTEXT|g" \
    -e "s|@NO_PERL_CPAN_FALLBACKS@|$NO_PERL_CPAN_FALLBACKS|g" \
    "$INPUT" >"$OUTPUT"

case "$INPUT" in
*.perl)
	chmod a+x "$OUTPUT";;
*)
	;;
esac
