#!/bin/sh
# Copyright (c) 2011, Google Inc.

test_description='diff --stat-count'
. ./test-lib.sh

test_expect_success setup '
	>a &&
	>b &&
	>c &&
	>d &&
	git add a b c d &&
	chmod +x c d &&
	echo a >a &&
	echo b >b &&
	cat >expect <<-\EOF
	 a |    1 +
	 b |    1 +
	 2 files changed, 2 insertions(+), 0 deletions(-)
	EOF
	git diff --stat --stat-count=2 >actual &&
	test_cmp expect actual
'

test_done
