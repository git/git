#!/bin/sh

test_description='rebase should handle arbitrary git message'

. ./test-lib.sh

cat >F <<\EOF
This is an example of a commit log message
that does not  conform to git commit convention.

It has two paragraphs, but its first paragraph is not friendly
to oneline summary format.
EOF

cat >G <<\EOF
commit log message containing a diff
EOF


test_expect_success setup '

	>file1 &&
	>file2 &&
	git add file1 file2 &&
	test_tick &&
	git commit -m "Initial commit" &&
	git branch diff-in-message &&

	git checkout -b multi-line-subject &&
	cat F >file2 &&
	git add file2 &&
	test_tick &&
	git commit -F F &&

	git cat-file commit HEAD | sed -e "1,/^\$/d" >F0 &&

	git checkout diff-in-message &&
	echo "commit log message containing a diff" >G &&
	echo "" >>G &&
	cat G >file2 &&
	git add file2 &&
	git diff --cached >>G &&
	test_tick &&
	git commit -F G &&

	git cat-file commit HEAD | sed -e "1,/^\$/d" >G0 &&

	git checkout master &&

	echo One >file1 &&
	test_tick &&
	git add file1 &&
	git commit -m "Second commit"
'

test_expect_success 'rebase commit with multi-line subject' '

	git rebase master multi-line-subject &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >F1 &&

	test_cmp F0 F1 &&
	test_cmp F F0
'

test_expect_success 'rebase commit with diff in message' '
	git rebase master diff-in-message &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >G1 &&
	test_cmp G0 G1 &&
	test_cmp G G0
'

test_done
