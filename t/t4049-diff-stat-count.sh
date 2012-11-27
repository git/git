#!/bin/sh
# Copyright (c) 2011, Google Inc.

test_description='diff --stat-count'
. ./test-lib.sh

test_expect_success 'setup' '
	>a &&
	>b &&
	>c &&
	>d &&
	git add a b c d &&
	git commit -m initial
'

test_expect_success 'limit output to 2 (simple)' '
	git reset --hard &&
	chmod +x c d &&
	echo a >a &&
	echo b >b &&
	cat >expect <<-\EOF
	 a | 1 +
	 b | 1 +
	 ...
	 4 files changed, 2 insertions(+)
	EOF
	git diff --stat --stat-count=2 >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'binary changes do not count in lines' '
	git reset --hard &&
	chmod +x c d &&
	echo a >a &&
	echo b >b &&
	cat "$TEST_DIRECTORY"/test-binary-1.png >d &&
	cat >expect <<-\EOF
	 a | 1 +
	 b | 1 +
	 ...
	 4 files changed, 2 insertions(+)
	EOF
	git diff --stat --stat-count=2 >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'exclude unmerged entries from total file count' '
	git reset --hard &&
	echo a >a &&
	echo b >b &&
	git ls-files -s a >x &&
	git rm -f d &&
	for stage in 1 2 3
	do
		sed -e "s/ 0	a/ $stage	d/" x
	done |
	git update-index --index-info &&
	echo d >d &&
	chmod +x c d &&
	cat >expect <<-\EOF
	 a | 1 +
	 b | 1 +
	 ...
	 4 files changed, 3 insertions(+)
	EOF
	git diff --stat --stat-count=2 >actual &&
	test_i18ncmp expect actual
'

test_done
