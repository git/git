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

test_expect_success 'mode-only change show as a 0-line change' '
	git reset --hard &&
	test_chmod +x b d &&
	echo a >a &&
	echo c >c &&
	cat >expect <<-\EOF &&
	 a | 1 +
	 b | 0
	 ...
	 4 files changed, 2 insertions(+)
	EOF
	git diff --stat --stat-count=2 HEAD >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'binary changes do not count in lines' '
	git reset --hard &&
	echo a >a &&
	echo c >c &&
	cat "$TEST_DIRECTORY"/test-binary-1.png >d &&
	cat >expect <<-\EOF &&
	 a | 1 +
	 c | 1 +
	 ...
	 3 files changed, 2 insertions(+)
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
	cat >expect <<-\EOF &&
	 a | 1 +
	 b | 1 +
	 ...
	 3 files changed, 3 insertions(+)
	EOF
	git diff --stat --stat-count=2 >actual &&
	test_i18ncmp expect actual
'

test_done
