#!/bin/sh
#
# Copyright (c) 2019 Rohit Ashiwal
#

test_description='tests to ensure compatibility between am and interactive backends'

. ./test-lib.sh

GIT_AUTHOR_DATE="1999-04-02T08:03:20+05:30"
export GIT_AUTHOR_DATE

# This is a special case in which both am and interactive backends
# provide the same outputs. It was done intentionally because
# --ignore-whitespace both the backends fall short of optimal
# behaviour.
test_expect_success 'setup' '
	git checkout -b topic &&
	q_to_tab >file <<-EOF &&
	line 1
	Qline 2
	line 3
	EOF
	git add file &&
	git commit -m "add file" &&
	q_to_tab >file <<-EOF &&
	line 1
	new line 2
	line 3
	EOF
	git commit -am "update file" &&
	git tag side &&

	git checkout --orphan master &&
	q_to_tab >file <<-EOF &&
	line 1
	        line 2
	line 3
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
	git cat-file commit HEAD | sed -e "/^\$/q" >head &&
	sed -ne "/^author /s/.*> //p" head >authortime &&
	sed -ne "/^committer /s/.*> //p" head >committertime &&
	test_cmp authortime committertime
'

test_expect_success '--committer-date-is-author-date works with interactive backend' '
	git rebase -f HEAD^ &&
	git rebase -i --committer-date-is-author-date HEAD^ &&
	git cat-file commit HEAD | sed -e "/^\$/q" >head &&
	sed -ne "/^author /s/.*> //p" head >authortime &&
	sed -ne "/^committer /s/.*> //p" head >committertime &&
	test_cmp authortime committertime
'

test_done
