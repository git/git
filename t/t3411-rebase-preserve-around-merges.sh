#!/bin/sh
#
# Copyright (c) 2008 Stephen Haberman
#

test_description='git rebase preserve merges

This test runs git rebase with and tries to squash a commit from after a merge
to before the merge.
'
. ./test-lib.sh

# Copy/paste from t3404-rebase-interactive.sh
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

# set up two branches like this:
#
# A1 - B1 - D1 - E1 - F1
#       \        /
#        -- C1 --

test_expect_success 'setup' '
	touch a &&
	touch b &&
	git add a &&
	git commit -m A1 &&
	git tag A1
	git add b &&
	git commit -m B1 &&
	git tag B1 &&
	git checkout -b branch &&
	touch c &&
	git add c &&
	git commit -m C1 &&
	git checkout master &&
	touch d &&
	git add d &&
	git commit -m D1 &&
	git merge branch &&
	touch f &&
	git add f &&
	git commit -m F1 &&
	git tag F1
'

# Should result in:
#
# A1 - B1 - D2 - E2
#       \        /
#        -- C1 --
#
test_expect_failure 'squash F1 into D1' '
	FAKE_LINES="1 squash 3 2" git rebase -i -p B1 &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse branch)" &&
	test "$(git rev-parse HEAD~2)" = "$(git rev-parse B1)" &&
	git tag E2
'

# Start with:
#
# A1 - B1 - D2 - E2
#  \
#   G1 ---- L1 ---- M1
#    \             /
#     H1 -- J1 -- K1
#      \         /
#        -- I1 --
#
# And rebase G1..M1 onto E2

test_expect_failure 'rebase two levels of merge' '
	git checkout -b branch2 A1 &&
	touch g &&
	git add g &&
	git commit -m G1 &&
	git checkout -b branch3 &&
	touch h
	git add h &&
	git commit -m H1 &&
	git checkout -b branch4 &&
	touch i &&
	git add i &&
	git commit -m I1 &&
	git tag I1 &&
	git checkout branch3 &&
	touch j &&
	git add j &&
	git commit -m J1 &&
	git merge I1 --no-commit &&
	git commit -m K1 &&
	git tag K1 &&
	git checkout branch2 &&
	touch l &&
	git add l &&
	git commit -m L1 &&
	git merge K1 --no-commit &&
	git commit -m M1 &&
	GIT_EDITOR=: git rebase -i -p E2 &&
	test "$(git rev-parse HEAD~3)" = "$(git rev-parse E2)" &&
	test "$(git rev-parse HEAD~2)" = "$(git rev-parse HEAD^2^2~2)" &&
	test "$(git rev-parse HEAD^2^1^1)" = "$(git rev-parse HEAD^2^2^1)"
'

test_done
