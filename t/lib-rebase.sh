#!/bin/sh

# After setting the fake editor with this function, you can
#
# - override the commit message with $FAKE_COMMIT_MESSAGE
# - amend the commit message with $FAKE_COMMIT_AMEND
# - check that non-commit messages have a certain line count with $EXPECT_COUNT
# - check the commit count in the commit message header with $EXPECT_HEADER_COUNT
# - rewrite a rebase -i script as directed by $FAKE_LINES.
#   $FAKE_LINES consists of a sequence of words separated by spaces.
#   The following word combinations are possible:
#
#   "<lineno>" -- add a "pick" line with the SHA1 taken from the
#       specified line.
#
#   "<cmd> <lineno>" -- add a line with the specified command
#       ("squash", "fixup", "edit", or "reword") and the SHA1 taken
#       from the specified line.
#
#   "#" -- Add a comment line.
#
#   ">" -- Add a blank line.

set_fake_editor () {
	echo "#!$SHELL_PATH" >fake-editor.sh
	cat >> fake-editor.sh <<\EOF
case "$1" in
*/COMMIT_EDITMSG)
	test -z "$EXPECT_HEADER_COUNT" ||
		test "$EXPECT_HEADER_COUNT" = "$(sed -n '1s/^# This is a combination of \(.*\) commits\./\1/p' < "$1")" ||
		exit
	test -z "$FAKE_COMMIT_MESSAGE" || echo "$FAKE_COMMIT_MESSAGE" > "$1"
	test -z "$FAKE_COMMIT_AMEND" || echo "$FAKE_COMMIT_AMEND" >> "$1"
	exit
	;;
esac
test -z "$EXPECT_COUNT" ||
	test "$EXPECT_COUNT" = $(sed -e '/^#/d' -e '/^$/d' < "$1" | wc -l) ||
	exit
test -z "$FAKE_LINES" && exit
grep -v '^#' < "$1" > "$1".tmp
rm -f "$1"
echo 'rebase -i script before editing:'
cat "$1".tmp
action=pick
for line in $FAKE_LINES; do
	case $line in
	squash|fixup|edit|reword)
		action="$line";;
	exec*)
		echo "$line" | sed 's/_/ /g' >> "$1";;
	"#")
		echo '# comment' >> "$1";;
	">")
		echo >> "$1";;
	*)
		sed -n "${line}s/^pick/$action/p" < "$1".tmp >> "$1"
		action=pick;;
	esac
done
echo 'rebase -i script after editing:'
cat "$1"
EOF

	test_set_editor "$(pwd)/fake-editor.sh"
	chmod a+x fake-editor.sh
}
