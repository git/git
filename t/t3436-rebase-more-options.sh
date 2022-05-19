#!/bin/sh
#
# Copyright (c) 2019 Rohit Ashiwal
#

test_description='tests to ensure compatibility between am and interactive backends'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

BUT_AUTHOR_DATE="1999-04-02T08:03:20+05:30"
export BUT_AUTHOR_DATE

# This is a special case in which both am and interactive backends
# provide the same output. It was done intentionally because
# both the backends fall short of optimal behaviour.
test_expect_success 'setup' '
	but checkout -b topic &&
	test_write_lines "line 1" "	line 2" "line 3" >file &&
	but add file &&
	but cummit -m "add file" &&

	test_write_lines "line 1" "new line 2" "line 3" >file &&
	but cummit -am "update file" &&
	but tag side &&
	test_cummit cummit1 foo foo1 &&
	test_cummit cummit2 foo foo2 &&
	test_cummit cummit3 foo foo3 &&

	but checkout --orphan main &&
	rm foo &&
	test_write_lines "line 1" "        line 2" "line 3" >file &&
	but cummit -am "add file" &&
	but tag main &&

	mkdir test-bin &&
	write_script test-bin/but-merge-test <<-\EOF
	exec but merge-recursive "$@"
	EOF
'

test_expect_success '--ignore-whitespace works with apply backend' '
	test_must_fail but rebase --apply main side &&
	but rebase --abort &&
	but rebase --apply --ignore-whitespace main side &&
	but diff --exit-code side
'

test_expect_success '--ignore-whitespace works with merge backend' '
	test_must_fail but rebase --merge main side &&
	but rebase --abort &&
	but rebase --merge --ignore-whitespace main side &&
	but diff --exit-code side
'

test_expect_success '--ignore-whitespace is remembered when continuing' '
	(
		set_fake_editor &&
		FAKE_LINES="break 1" but rebase -i --ignore-whitespace \
			main side &&
		but rebase --continue
	) &&
	but diff --exit-code side
'

test_ctime_is_atime () {
	but log $1 --format="$BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> %ai" >authortime &&
	but log $1 --format="%cn <%ce> %ci" >cummittertime &&
	test_cmp authortime cummittertime
}

test_expect_success '--cummitter-date-is-author-date works with apply backend' '
	BUT_AUTHOR_DATE="@1234 +0300" but cummit --amend --reset-author &&
	but rebase --apply --cummitter-date-is-author-date HEAD^ &&
	test_ctime_is_atime -1
'

test_expect_success '--cummitter-date-is-author-date works with merge backend' '
	BUT_AUTHOR_DATE="@1234 +0300" but cummit --amend --reset-author &&
	but rebase -m --cummitter-date-is-author-date HEAD^ &&
	test_ctime_is_atime -1
'

test_expect_success '--cummitter-date-is-author-date works when rewording' '
	BUT_AUTHOR_DATE="@1234 +0300" but cummit --amend --reset-author &&
	(
		set_fake_editor &&
		FAKE_CUMMIT_MESSAGE=edited \
			FAKE_LINES="reword 1" \
			but rebase -i --cummitter-date-is-author-date HEAD^
	) &&
	test_write_lines edited "" >expect &&
	but log --format="%B" -1 >actual &&
	test_cmp expect actual &&
	test_ctime_is_atime -1
'

test_expect_success '--cummitter-date-is-author-date works with rebase -r' '
	but checkout side &&
	BUT_AUTHOR_DATE="@1234 +0300" but merge --no-ff cummit3 &&
	but rebase -r --root --cummitter-date-is-author-date &&
	test_ctime_is_atime
'

test_expect_success '--cummitter-date-is-author-date works when forking merge' '
	but checkout side &&
	BUT_AUTHOR_DATE="@1234 +0300" but merge --no-ff cummit3 &&
	PATH="./test-bin:$PATH" but rebase -r --root --strategy=test \
					--cummitter-date-is-author-date &&
	test_ctime_is_atime
'

test_expect_success '--cummitter-date-is-author-date works when cummitting conflict resolution' '
	but checkout cummit2 &&
	BUT_AUTHOR_DATE="@1980 +0000" but cummit --amend --only --reset-author &&
	test_must_fail but rebase -m --cummitter-date-is-author-date \
		--onto HEAD^^ HEAD^ &&
	echo resolved > foo &&
	but add foo &&
	but rebase --continue &&
	test_ctime_is_atime -1
'

# Checking for +0000 in the author date is sufficient since the
# default timezone is UTC but the timezone used while cummitting is
# +0530. The inverted logic in the grep is necessary to check all the
# author dates in the file.
test_atime_is_ignored () {
	but log $1 --format=%ai >authortime &&
	! grep -v +0000 authortime
}

test_expect_success '--reset-author-date works with apply backend' '
	but cummit --amend --date="$BUT_AUTHOR_DATE" &&
	but rebase --apply --reset-author-date HEAD^ &&
	test_atime_is_ignored -1
'

test_expect_success '--reset-author-date works with merge backend' '
	but cummit --amend --date="$BUT_AUTHOR_DATE" &&
	but rebase --reset-author-date -m HEAD^ &&
	test_atime_is_ignored -1
'

test_expect_success '--reset-author-date works after conflict resolution' '
	test_must_fail but rebase --reset-author-date -m \
		--onto cummit2^^ cummit2^ cummit2 &&
	echo resolved >foo &&
	but add foo &&
	but rebase --continue &&
	test_atime_is_ignored -1
'

test_expect_success '--reset-author-date works with rebase -r' '
	but checkout side &&
	but merge --no-ff cummit3 &&
	but rebase -r --root --reset-author-date &&
	test_atime_is_ignored
'

test_expect_success '--reset-author-date with --cummitter-date-is-author-date works' '
	test_must_fail but rebase -m --cummitter-date-is-author-date \
		--reset-author-date --onto cummit2^^ cummit2^ cummit3 &&
	but checkout --theirs foo &&
	but add foo &&
	but rebase --continue &&
	test_ctime_is_atime -2 &&
	test_atime_is_ignored -2
'

test_expect_success 'reset-author-date with --cummitter-date-is-author-date works when rewording' '
	BUT_AUTHOR_DATE="@1234 +0300" but cummit --amend --reset-author &&
	(
		set_fake_editor &&
		FAKE_CUMMIT_MESSAGE=edited \
			FAKE_LINES="reword 1" \
			but rebase -i --cummitter-date-is-author-date \
				--reset-author-date HEAD^
	) &&
	test_write_lines edited "" >expect &&
	but log --format="%B" -1 >actual &&
	test_cmp expect actual &&
	test_atime_is_ignored -1
'

test_expect_success '--reset-author-date --cummitter-date-is-author-date works when forking merge' '
	BUT_SEQUENCE_EDITOR="echo \"merge -C $(but rev-parse HEAD) cummit3\">" \
		PATH="./test-bin:$PATH" but rebase -i --strategy=test \
				--reset-author-date \
				--cummitter-date-is-author-date side side &&
	test_ctime_is_atime -1 &&
	test_atime_is_ignored -1
 '

test_expect_success '--ignore-date is an alias for --reset-author-date' '
	but cummit --amend --date="$BUT_AUTHOR_DATE" &&
	but rebase --apply --ignore-date HEAD^ &&
	but cummit --allow-empty -m empty --date="$BUT_AUTHOR_DATE" &&
	but rebase -m --ignore-date HEAD^ &&
	test_atime_is_ignored -2
'

# This must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
