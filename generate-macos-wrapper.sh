#!/bin/sh

set -e

if test "$#" -ne 3
then
	echo >&2 "usage: $0 <OUTPUT> <BUILD_OPTIONS> <VERSION_FILE>"
	exit 1
fi

OUTPUT="$1"
BUILD_OPTIONS="$2"
VERSION_FILE="$3"

. "$BUILD_OPTIONS"

rm -f "$OUTPUT" "$OUTPUT+"

(
	echo "#!$SHELL_PATH"
	cat "$BUILD_OPTIONS" "$VERSION_FILE"
	cat <<-'EOF'
	if test "z$*" = zversion ||
	   test "z$*" = z--version
	then
		echo "git-gui version $GITGUI_VERSION"
	else
		libdir="${GIT_GUI_LIB_DIR:-$GITGUI_LIBDIR}"
		exec "$libdir/Git Gui.app/Contents/MacOS/$(basename "$TKEXECUTABLE")" "$0" "$@"
	fi
	EOF
) >"$OUTPUT+"

chmod +x "$OUTPUT+"
mv "$OUTPUT+" "$OUTPUT"
