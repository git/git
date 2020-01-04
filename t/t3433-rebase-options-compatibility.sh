#!/bin/sh
#
# Copyright (c) 2019 Rohit Ashiwal
#

test_description='tests to ensure compatibility between am and interactive backends'

. ./test-lib.sh

GIT_AUTHOR_DATE="1999-04-02T08:03:20+05:30"
export GIT_AUTHOR_DATE

# This is a special case in which both am and interactive backends
# provide the same output. It was done intentionally because
# both the backends fall short of optimal behaviour.
test_expect_success 'setup' '
	git checkout -b topic &&
	q_to_tab >file <<-\EOF &&
	line 1
	Qline 2
	line 3
	EOF
	git add file &&
	git commit -m "add file" &&
	cat >file <<-\EOF &&
	line 1
	new line 2
	line 3
	EOF
	git commit -am "update file" &&
	git tag side &&
	test_commit commit1 foo foo1 &&
	test_commit commit2 foo foo2 &&
	test_commit commit3 foo foo3 &&

	git checkout --orphan master &&
	git rm --cached foo &&
	rm foo &&
	sed -e "s/^|//" >file <<-\EOF &&
	|line 1
	|        line 2
	|line 3
	EOF
	git add file &&
	git commit -m "add file" &&
	git tag main
'

test_expect_success '--ignore-whitespace works with am backend' '
	cat >expect <<-\EOF &&
	line 1
	new line 2
	line 3
	EOF
	test_must_fail git rebase main side &&
	git rebase --abort &&
	git rebase --ignore-whitespace main side &&
	test_cmp expect file
'

test_expect_success '--ignore-whitespace works with interactive backend' '
	cat >expect <<-\EOF &&
	line 1
	new line 2
	line 3
	EOF
	test_must_fail git rebase --merge main side &&
	git rebase --abort &&
	git rebase --merge --ignore-whitespace main side &&
	test_cmp expect file
'

test_expect_success '--committer-date-is-author-date works with am backend' '
	git commit --amend &&
	git rebase --committer-date-is-author-date HEAD^ &&
	git show HEAD --pretty="format:%ai" >authortime &&
	git show HEAD --pretty="format:%ci" >committertime &&
	test_cmp authortime committertime
'

test_expect_success '--committer-date-is-author-date works with interactive backend' '
	git commit --amend &&
	git rebase -i --committer-date-is-author-date HEAD^ &&
	git show HEAD --pretty="format:%ai" >authortime &&
	git show HEAD --pretty="format:%ci" >committertime &&
	test_cmp authortime committertime
'

test_expect_success '--committer-date-is-author-date works with rebase -r' '
	git checkout side &&
	git merge --no-ff commit3 &&
	git rebase -r --root --committer-date-is-author-date &&
	git rev-list HEAD >rev_list &&
	while read HASH
	do
		git show $HASH --pretty="format:%ai" >authortime
		git show $HASH --pretty="format:%ci" >committertime
		test_cmp authortime committertime
	done <rev_list
'

# Checking for +0000 in author time is enough since default
# timezone is UTC, but the timezone used while committing
# sets to +0530.
test_expect_success '--ignore-date works with am backend' '
	git commit --amend --date="$GIT_AUTHOR_DATE" &&
	git rebase --ignore-date HEAD^ &&
	git show HEAD --pretty="format:%ai" >authortime &&
	grep "+0000" authortime
'

test_expect_success '--ignore-date works with interactive backend' '
	git commit --amend --date="$GIT_AUTHOR_DATE" &&
	git rebase --ignore-date -i HEAD^ &&
	git show HEAD --pretty="format:%ai" >authortime &&
	grep "+0000" authortime
'

test_expect_success '--ignore-date works with rebase -r' '
	git checkout side &&
	git merge --no-ff commit3 &&
	git rebase -r --root --ignore-date &&
	git rev-list HEAD >rev_list &&
	while read HASH
	do
		git show $HASH --pretty="format:%ai" >authortime
		grep "+0000" authortime
	done <rev_list
'

test_done
