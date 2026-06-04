#!/bin/sh

if test "$#" -lt 2
then
	echo >&2 "USAGE: $0 <OUTPUT> <INPUT>..."
	exit 1
fi

OUTPUT="$1"
shift

cat "$@" >"$OUTPUT"
