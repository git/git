#!/bin/sh

test_description='rebase should handle arbitrary but message'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

cat >F <<\EOF
This is an example of a cummit log message
that does not  conform to but cummit convention.

It has two paragraphs, but its first paragraph is not friendly
to oneline summary format.
EOF

cat >G <<\EOF
cummit log message containing a diff
EOF


test_expect_success setup '

	>file1 &&
	>file2 &&
	but add file1 file2 &&
	test_tick &&
	but cummit -m "Initial cummit" &&
	but branch diff-in-message &&
	but branch empty-message-merge &&

	but checkout -b multi-line-subject &&
	cat F >file2 &&
	but add file2 &&
	test_tick &&
	but cummit -F F &&

	but cat-file commit HEAD | sed -e "1,/^\$/d" >F0 &&

	but checkout diff-in-message &&
	echo "cummit log message containing a diff" >G &&
	echo "" >>G &&
	cat G >file2 &&
	but add file2 &&
	but diff --cached >>G &&
	test_tick &&
	but cummit -F G &&

	but cat-file commit HEAD | sed -e "1,/^\$/d" >G0 &&

	but checkout empty-message-merge &&
	echo file3 >file3 &&
	but add file3 &&
	but cummit --allow-empty-message -m "" &&

	but checkout main &&

	echo One >file1 &&
	test_tick &&
	but add file1 &&
	but cummit -m "Second cummit"
'

test_expect_success 'rebase cummit with multi-line subject' '

	but rebase main multi-line-subject &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" >F1 &&

	test_cmp F0 F1 &&
	test_cmp F F0
'

test_expect_success 'rebase cummit with diff in message' '
	but rebase main diff-in-message &&
	but cat-file commit HEAD | sed -e "1,/^$/d" >G1 &&
	test_cmp G0 G1 &&
	test_cmp G G0
'

test_expect_success 'rebase -m cummit with empty message' '
	but rebase -m main empty-message-merge
'

test_expect_success 'rebase -i cummit with empty message' '
	but checkout diff-in-message &&
	set_fake_editor &&
	test_must_fail env FAKE_CUMMIT_MESSAGE=" " FAKE_LINES="reword 1" \
		but rebase -i HEAD^
'

test_done
