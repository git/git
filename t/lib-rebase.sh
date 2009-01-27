#!/bin/sh

set_fake_editor () {
	echo "#!$SHELL_PATH" >fake-editor.sh
	cat >> fake-editor.sh <<\EOF
case "$1" in
*/COMMIT_EDITMSG)
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
cat "$1".tmp
action=pick
for line in $FAKE_LINES; do
	case $line in
	squash|edit)
		action="$line";;
	*)
		echo sed -n "${line}s/^pick/$action/p"
		sed -n "${line}p" < "$1".tmp
		sed -n "${line}s/^pick/$action/p" < "$1".tmp >> "$1"
		action=pick;;
	esac
done
EOF

	test_set_editor "$(pwd)/fake-editor.sh"
	chmod a+x fake-editor.sh
}
