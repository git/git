#!/bin/sh

test_description='rebase should not insist on git message convention'

. ./test-lib.sh

cat >F <<\EOF
This is an example of a commit log message
that does not  conform to git commit convention.

It has two paragraphs, but its first paragraph is not friendly
to oneline summary format.
EOF

test_expect_success setup '

	>file1 &&
	>file2 &&
	git add file1 file2 &&
	test_tick &&
	git commit -m "Initial commit" &&

	git checkout -b side &&
	cat F >file2 &&
	git add file2 &&
	test_tick &&
	git commit -F F &&

	git cat-file commit HEAD | sed -e "1,/^\$/d" >F0 &&

	git checkout master &&

	echo One >file1 &&
	test_tick &&
	git add file1 &&
	git commit -m "Second commit"
'

test_expect_success rebase '

	git rebase master side &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >F1 &&

	diff -u F0 F1 &&
	diff -u F F0
'

test_done
