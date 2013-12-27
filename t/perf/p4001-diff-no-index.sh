#!/bin/sh

test_description="Test diff --no-index performance"

. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

file1=$(git ls-files | tail -n 2 | head -1)
file2=$(git ls-files | tail -n 1 | head -1)

test_expect_success "empty files, so they take no time to diff" "
	echo >$file1 &&
	echo >$file2
"

test_perf "diff --no-index" "
	git diff --no-index $file1 $file2 >/dev/null
"

test_done
