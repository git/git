#!/bin/sh

test_description='git log with filter options limiting the output'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup test' '
	git init &&
	echo a >file &&
	git add file &&
	GIT_COMMITTER_DATE="2021-02-01 00:00" git commit -m init &&
	echo a >>file &&
	git add file &&
	GIT_COMMITTER_DATE="2022-02-01 00:00" git commit -m first &&
	echo a >>file &&
	git add file &&
	GIT_COMMITTER_DATE="2021-03-01 00:00" git commit -m second &&
	echo a >>file &&
	git add file &&
	GIT_COMMITTER_DATE="2022-03-01 00:00" git commit -m third
'

test_expect_success 'git log --since-as-filter=...' '
	git log --since-as-filter="2022-01-01" --format=%s >actual &&
	cat >expect <<-\EOF &&
	third
	first
	EOF
	test_cmp expect actual
'

test_expect_success 'git log --children --since-as-filter=...' '
	git log --children --since-as-filter="2022-01-01" --format=%s >actual &&
	cat >expect <<-\EOF &&
	third
	first
	EOF
	test_cmp expect actual
'

test_done
