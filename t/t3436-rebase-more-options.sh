#!/bin/sh
#
# Copyright (c) 2019 Rohit Ashiwal
#

test_description='tests to ensure compatibility between am and interactive backends'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

GIT_AUTHOR_DATE="1999-04-02T08:03:20+05:30"
export GIT_AUTHOR_DATE

# This is a special case in which both am and interactive backends
# provide the same output. It was done intentionally because
# both the backends fall short of optimal behaviour.
test_expect_success 'setup' '
	git checkout -b topic &&
	test_write_lines "line 1" "	line 2" "line 3" >file &&
	git add file &&
	git commit -m "add file" &&

	test_write_lines "line 1" "new line 2" "line 3" >file &&
	git commit -am "update file" &&
	git tag side &&
	test_commit commit1 foo foo1 &&
	test_commit commit2 foo foo2 &&
	test_commit commit3 foo foo3 &&

	git checkout --orphan main &&
	rm foo &&
	test_write_lines "line 1" "        line 2" "line 3" >file &&
	git commit -am "add file" &&
	git tag main &&

	mkdir test-bin &&
	write_script test-bin/git-merge-test <<-\EOF
	exec git merge-recursive "$@"
	EOF
'

test_expect_success '--ignore-whitespace works with apply backend' '
	test_must_fail git rebase --apply main side &&
	git rebase --abort &&
	git rebase --apply --ignore-whitespace main side &&
	git diff --exit-code side
'

test_expect_success '--ignore-whitespace works with merge backend' '
	test_must_fail git rebase --merge main side &&
	git rebase --abort &&
	git rebase --merge --ignore-whitespace main side &&
	git diff --exit-code side
'

test_expect_success '--ignore-whitespace is remembered when continuing' '
	(
		set_fake_editor &&
		FAKE_LINES="break 1" git rebase -i --ignore-whitespace \
			main side &&
		git rebase --continue
	) &&
	git diff --exit-code side
'

test_ctime_is_atime () {
	git log $1 --format="$GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> %ai" >authortime &&
	git log $1 --format="%cn <%ce> %ci" >committertime &&
	test_cmp authortime committertime
}

test_expect_success '--committer-date-is-author-date works with apply backend' '
	GIT_AUTHOR_DATE="@1234 +0300" git commit --amend --reset-author &&
	git rebase --apply --committer-date-is-author-date HEAD^ &&
	test_ctime_is_atime -1
'

test_expect_success '--committer-date-is-author-date works with merge backend' '
	GIT_AUTHOR_DATE="@1234 +0300" git commit --amend --reset-author &&
	git rebase -m --committer-date-is-author-date HEAD^ &&
	test_ctime_is_atime -1
'

test_expect_success '--committer-date-is-author-date works with rebase -r' '
	git checkout side &&
	GIT_AUTHOR_DATE="@1234 +0300" git merge --no-ff commit3 &&
	git rebase -r --root --committer-date-is-author-date &&
	test_ctime_is_atime
'

test_expect_success '--committer-date-is-author-date works when forking merge' '
	git checkout side &&
	GIT_AUTHOR_DATE="@1234 +0300" git merge --no-ff commit3 &&
	PATH="./test-bin:$PATH" git rebase -r --root --strategy=test \
					--committer-date-is-author-date &&
	test_ctime_is_atime
'

test_expect_success '--committer-date-is-author-date works when committing conflict resolution' '
	git checkout commit2 &&
	GIT_AUTHOR_DATE="@1980 +0000" git commit --amend --only --reset-author &&
	test_must_fail git rebase -m --committer-date-is-author-date \
		--onto HEAD^^ HEAD^ &&
	echo resolved > foo &&
	git add foo &&
	git rebase --continue &&
	test_ctime_is_atime -1
'

# Checking for +0000 in the author date is sufficient since the
# default timezone is UTC but the timezone used while committing is
# +0530. The inverted logic in the grep is necessary to check all the
# author dates in the file.
test_atime_is_ignored () {
	git log $1 --format=%ai >authortime &&
	! grep -v +0000 authortime
}

test_expect_success '--reset-author-date works with apply backend' '
	git commit --amend --date="$GIT_AUTHOR_DATE" &&
	git rebase --apply --reset-author-date HEAD^ &&
	test_atime_is_ignored -1
'

test_expect_success '--reset-author-date works with merge backend' '
	git commit --amend --date="$GIT_AUTHOR_DATE" &&
	git rebase --reset-author-date -m HEAD^ &&
	test_atime_is_ignored -1
'

test_expect_success '--reset-author-date works after conflict resolution' '
	test_must_fail git rebase --reset-author-date -m \
		--onto commit2^^ commit2^ commit2 &&
	echo resolved >foo &&
	git add foo &&
	git rebase --continue &&
	test_atime_is_ignored -1
'

test_expect_success '--reset-author-date works with rebase -r' '
	git checkout side &&
	git merge --no-ff commit3 &&
	git rebase -r --root --reset-author-date &&
	test_atime_is_ignored
'

test_expect_success '--reset-author-date with --committer-date-is-author-date works' '
	test_must_fail git rebase -m --committer-date-is-author-date \
		--reset-author-date --onto commit2^^ commit2^ commit3 &&
	git checkout --theirs foo &&
	git add foo &&
	git rebase --continue &&
	test_ctime_is_atime -2 &&
	test_atime_is_ignored -2
'

test_expect_success '--reset-author-date --committer-date-is-author-date works when forking merge' '
	GIT_SEQUENCE_EDITOR="echo \"merge -C $(git rev-parse HEAD) commit3\">" \
		PATH="./test-bin:$PATH" git rebase -i --strategy=test \
				--reset-author-date \
				--committer-date-is-author-date side side &&
	test_ctime_is_atime -1 &&
	test_atime_is_ignored -1
 '

test_expect_success '--ignore-date is an alias for --reset-author-date' '
	git commit --amend --date="$GIT_AUTHOR_DATE" &&
	git rebase --apply --ignore-date HEAD^ &&
	git commit --allow-empty -m empty --date="$GIT_AUTHOR_DATE" &&
	git rebase -m --ignore-date HEAD^ &&
	test_atime_is_ignored -2
'

# This must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
