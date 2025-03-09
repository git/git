#!/bin/sh

set -e

WISH=$(echo "$1" | sed 's/|/\\|/g')
INPUT="$2"
OUTPUT="$3"

sed -e "1,3s|^exec .* \"\$0\"|exec $WISH \"\$0\"|" "$INPUT" >"$OUTPUT"+
chmod a+x "$OUTPUT"+
mv "$OUTPUT"+ "$OUTPUT"
