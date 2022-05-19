#!/bin/sh
# Copyright (c) 2011, Google Inc.

test_description='diff --stat-count'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	>a &&
	>b &&
	>c &&
	>d &&
	but add a b c d &&
	but cummit -m initial
'

test_expect_success 'mode-only change show as a 0-line change' '
	but reset --hard &&
	test_chmod +x b d &&
	echo a >a &&
	echo c >c &&
	cat >expect <<-\EOF &&
	 a | 1 +
	 b | 0
	 ...
	 4 files changed, 2 insertions(+)
	EOF
	but diff --stat --stat-count=2 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'binary changes do not count in lines' '
	but reset --hard &&
	echo a >a &&
	echo c >c &&
	cat "$TEST_DIRECTORY"/test-binary-1.png >d &&
	cat >expect <<-\EOF &&
	 a | 1 +
	 c | 1 +
	 ...
	 3 files changed, 2 insertions(+)
	EOF
	but diff --stat --stat-count=2 >actual &&
	test_cmp expect actual
'

test_expect_success 'exclude unmerged entries from total file count' '
	but reset --hard &&
	echo a >a &&
	echo b >b &&
	but ls-files -s a >x &&
	but rm -f d &&
	for stage in 1 2 3
	do
		sed -e "s/ 0	a/ $stage	d/" x || return 1
	done |
	but update-index --index-info &&
	echo d >d &&
	cat >expect <<-\EOF &&
	 a | 1 +
	 b | 1 +
	 ...
	 3 files changed, 3 insertions(+)
	EOF
	but diff --stat --stat-count=2 >actual &&
	test_cmp expect actual
'

test_done
