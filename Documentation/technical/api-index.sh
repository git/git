#!/bin/sh

if test $# -ne 2
then
	echo >&2 "USAGE: $0 <SOURCE_DIR> <OUTPUT>"
	exit 1
fi

SOURCE_DIR="$1"
OUTPUT="$2"

(
	cd "$SOURCE_DIR"

	c=////////////////////////////////////////////////////////////////
	skel=api-index-skel.adoc
	sed -e '/^\/\/ table of contents begin/q' "$skel"
	echo "$c"

	ls api-*.adoc |
	while read filename
	do
		case "$filename" in
		api-index-skel.adoc | api-index.adoc) continue ;;
		esac
		title=$(sed -e 1q "$filename")
		html=${filename%.adoc}.html
		echo "* link:$html[$title]"
	done
	echo "$c"
	sed -n -e '/^\/\/ table of contents end/,$p' "$skel"
) >"$OUTPUT"+

if test -f "$OUTPUT" && cmp "$OUTPUT" "$OUTPUT"+ >/dev/null
then
	rm -f "$OUTPUT"+
else
	mv "$OUTPUT"+ "$OUTPUT"
fi
