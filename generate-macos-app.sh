#!/bin/sh

set -e

SOURCE_DIR="$1"
OUTPUT="$2"
BUILD_OPTIONS="$3"
VERSION_FILE="$4"

. "$BUILD_OPTIONS"
. "$VERSION_FILE"

rm -rf "$OUTPUT" "$OUTPUT+"

mkdir -p "$OUTPUT+/Contents/MacOS"
mkdir -p "$OUTPUT+/Contents/Resources/Scripts"

cp "$TKEXECUTABLE" "$OUTPUT+/Contents/MacOS"
cp "$SOURCE_DIR/macosx/git-gui.icns" "$OUTPUT+/Contents/Resources"
sed \
	-e "s/@@GITGUI_VERSION@@/$GITGUI_VERSION/g" \
	-e "s/@@GITGUI_TKEXECUTABLE@@/$(basename "$TKEXECUTABLE")/g" \
	"$SOURCE_DIR/macosx/Info.plist" \
	>"$OUTPUT+/Contents/Info.plist"
sed \
	-e "s|@@gitexecdir@@|$GITGUI_GITEXECDIR|" \
	-e "s|@@GITGUI_LIBDIR@@|$GITGUI_LIBDIR|" \
	"$SOURCE_DIR/macosx/AppMain.tcl" \
	>"$OUTPUT+/Contents/Resources/Scripts/AppMain.tcl"
mv "$OUTPUT+" "$OUTPUT"
