# Helper functions used by interactive rebase tests.

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
#       ("squash", "fixup", "edit", "reword" or "drop") and the SHA1 taken
#       from the specified line.
#
#   "exec_cmd_with_args" -- add an "exec cmd with args" line.
#
#   "#" -- Add a comment line.
#
#   ">" -- Add a blank line.

set_fake_editor () {
	write_script fake-editor.sh <<-\EOF
	case "$1" in
	*/COMMIT_EDITMSG)
		test -z "$EXPECT_HEADER_COUNT" ||
			test "$EXPECT_HEADER_COUNT" = "$(sed -n '1s/^# This is a combination of \(.*\) commits\./\1/p' < "$1")" ||
			test "# # GETTEXT POISON #" = "$(sed -n '1p' < "$1")" ||
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
		squash|fixup|edit|reword|drop)
			action="$line";;
		exec*)
			echo "$line" | sed 's/_/ /g' >> "$1";;
		"#")
			echo '# comment' >> "$1";;
		">")
			echo >> "$1";;
		bad)
			action="badcmd";;
		fakesha)
			echo "$action XXXXXXX False commit" >> "$1"
			action=pick;;
		*)
			sed -n "${line}s/^pick/$action/p" < "$1".tmp >> "$1"
			action=pick;;
		esac
	done
	echo 'rebase -i script after editing:'
	cat "$1"
	EOF

	test_set_editor "$(pwd)/fake-editor.sh"
}

# After set_cat_todo_editor, rebase -i will write the todo list (ignoring
# blank lines and comments) to stdout, and exit failure (so you should run
# it with test_must_fail).  This can be used to verify the expected user
# experience, for todo list changes that do not affect the outcome of
# rebase; or as an extra check in addition to checking the outcome.

set_cat_todo_editor () {
	write_script fake-editor.sh <<-\EOF
	grep "^[^#]" "$1"
	exit 1
	EOF
	test_set_editor "$(pwd)/fake-editor.sh"
}

# checks that the revisions in "$2" represent a linear range with the
# subjects in "$1"
test_linear_range () {
	revlist_merges=$(git rev-list --merges "$2") &&
	test -z "$revlist_merges" &&
	expected=$1
	set -- $(git log --reverse --format=%s "$2")
	test "$expected" = "$*"
}

reset_rebase () {
	test_might_fail git rebase --abort &&
	git reset --hard &&
	git clean -f
}

cherry_pick () {
	git cherry-pick -n "$2" &&
	git commit -m "$1" &&
	git tag "$1"
}

revert () {
	git revert -n "$2" &&
	git commit -m "$1" &&
	git tag "$1"
}

make_empty () {
	git commit --allow-empty -m "$1" &&
	git tag "$1"
}
