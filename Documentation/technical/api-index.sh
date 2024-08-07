#!/bin/sh

(
	c=////////////////////////////////////////////////////////////////
	skel=api-index-skel.txt
	sed -e '/^\/\/ table of contents begin/q' "$skel"
	echo "$c"

	ls api-*.txt |
	while read filename
	do
		case "$filename" in
		api-index-skel.txt | api-index.txt) continue ;;
		esac
		title=$(sed -e 1q "$filename")
		html=${filename%.txt}.html
		echo "* link:$html[$title]"
	done
	echo "$c"
	sed -n -e '/^\/\/ table of contents end/,$p' "$skel"
) >api-index.txt+

if test -f api-index.txt && cmp api-index.txt api-index.txt+ >/dev/null
then
	rm -f api-index.txt+
else
	mv api-index.txt+ api-index.txt
fi
