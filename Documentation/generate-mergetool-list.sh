#!/bin/sh

if test "$#" -ne 3
then
	echo >&2 "USAGE: $0 <SOURCE_DIR> <MODE> <OUTPUT>"
	exit 1
fi

SOURCE_DIR="$1"
TOOL_MODE="$2"
OUTPUT="$3"
MERGE_TOOLS_DIR="$SOURCE_DIR/mergetools"

(
	. "$SOURCE_DIR"/git-mergetool--lib.sh &&
	show_tool_names can_$TOOL_MODE
) | sed -e "s/\([a-z0-9]*\)/\`\1\`;;/" >"$OUTPUT"
