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

	git checkout --orphan master &&
	rm foo &&
	test_write_lines "line 1" "        line 2" "line 3" >file &&
	git commit -am "add file" &&
	git tag main &&

	mkdir test-bin &&
	write_script test-bin/git-merge-test <<-\EOF
	exec git-merge-recursive "$@"
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
	git log $1 --format=%ai >authortime &&
	git log $1 --format=%ci >committertime &&
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

# This must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
