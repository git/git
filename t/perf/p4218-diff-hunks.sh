#!/bin/sh

test_description='diff-hunks store performance'
. ./perf-lib.sh

test_perf_default_repo

# Pick a file to blame pseudo-randomly. The sort key is the blob
# hash, so it is stable.
test_expect_success 'select a file' '
	git ls-tree -r HEAD | grep ^100644 |
	sort -k 3 | head -n 1 | cut -f 2 >filelist
'

file=$(cat filelist)
export file

# Warm the store the way an owner would: a stat walk with writing on.
test_perf 'warm the store' '
	git diff-hunks clear &&
	GIT_DIFF_HUNKS_WRITE=1 git log --all --stat >/dev/null
'

test_expect_success 'ensure the store is warm for the timed reads' '
	GIT_DIFF_HUNKS_WRITE=1 git log --all --stat >/dev/null
'

test_perf 'log --stat -1000 (store)' '
	git log --stat -1000 >/dev/null
'

test_perf 'log --stat -1000 (no store)' '
	git -c core.diffhunks=false log --stat -1000 >/dev/null
'

test_perf 'blame $file (store)' '
	git blame "$file" >/dev/null
'

test_perf 'blame $file (no store)' '
	git -c core.diffhunks=false blame "$file" >/dev/null
'

test_expect_success 'clean up store' '
	git diff-hunks clear
'

test_done
