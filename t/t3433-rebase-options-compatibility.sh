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
	q_to_tab >file <<-EOF &&
	line 1
	Qline 2
	line 3
	EOF
	git add file &&
	git commit -m "add file" &&
	cat >file <<-EOF &&
	line 1
	new line 2
	line 3
	EOF
	git commit -am "update file" &&
	git tag side &&

	git checkout --orphan master &&
	sed -e "s/^|//" >file <<-EOF &&
	|line 1
	|        line 2
	|line 3
	EOF
	git add file &&
	git commit -m "add file" &&
	git tag main
'

test_expect_success '--ignore-whitespace works with am backend' '
	cat >expect <<-EOF &&
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
	cat >expect <<-EOF &&
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
	git rebase -f HEAD^ &&
	git rebase --committer-date-is-author-date HEAD^ &&
	git show HEAD --pretty="format:%at" >authortime &&
	git show HEAD --pretty="format:%ct" >committertime &&
	test_cmp authortime committertime
'

test_expect_success '--committer-date-is-author-date works with interactive backend' '
	git rebase -f HEAD^ &&
	git rebase -i --committer-date-is-author-date HEAD^ &&
	git show HEAD --pretty="format:%at" >authortime &&
	git show HEAD --pretty="format:%ct" >committertime &&
	test_cmp authortime committertime
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
test_done
