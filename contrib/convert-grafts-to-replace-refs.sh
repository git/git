#!/bin/sh

# You should execute this script in the repository where you
# want to convert grafts to replace refs.

GRAFTS_FILE="${GIT_DIR:-.git}/info/grafts"

. $(git --exec-path)/git-sh-setup

test -f "$GRAFTS_FILE" || die "Could not find graft file: '$GRAFTS_FILE'"

grep '^[^# ]' "$GRAFTS_FILE" |
while read definition
do
	if test -n "$definition"
	then
		echo "Converting: $definition"
		git replace --graft $definition ||
			die "Conversion failed for: $definition"
	fi
done

mv "$GRAFTS_FILE" "$GRAFTS_FILE.bak" ||
	die "Could not rename '$GRAFTS_FILE' to '$GRAFTS_FILE.bak'"

echo "Success!"
echo "All the grafts in '$GRAFTS_FILE' have been converted to replace refs!"
echo "The grafts file '$GRAFTS_FILE' has been renamed: '$GRAFTS_FILE.bak'"
