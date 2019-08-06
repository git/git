#!/bin/sh
#
# Copyright (c) 2019 Rohit Ashiwal
#

test_description='tests to ensure compatibility between am and interactive backends'

. ./test-lib.sh

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

test_done
