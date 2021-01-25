#!/bin/sh

test_description='rebase should handle arbitrary git message'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

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
	git branch empty-message-merge &&

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

	git checkout empty-message-merge &&
	echo file3 >file3 &&
	git add file3 &&
	git commit --allow-empty-message -m "" &&

	git checkout main &&

	echo One >file1 &&
	test_tick &&
	git add file1 &&
	git commit -m "Second commit"
'

test_expect_success 'rebase commit with multi-line subject' '

	git rebase main multi-line-subject &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >F1 &&

	test_cmp F0 F1 &&
	test_cmp F F0
'

test_expect_success 'rebase commit with diff in message' '
	git rebase main diff-in-message &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >G1 &&
	test_cmp G0 G1 &&
	test_cmp G G0
'

test_expect_success 'rebase -m commit with empty message' '
	git rebase -m main empty-message-merge
'

test_expect_success 'rebase -i commit with empty message' '
	git checkout diff-in-message &&
	set_fake_editor &&
	test_must_fail env FAKE_COMMIT_MESSAGE=" " FAKE_LINES="reword 1" \
		git rebase -i HEAD^
'

test_done
