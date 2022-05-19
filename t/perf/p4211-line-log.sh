#!/bin/sh

test_description='Tests log -L performance'
. ./perf-lib.sh

test_perf_default_repo

# Pick a file to log pseudo-randomly.  The sort key is the blob hash,
# so it is stable.
test_expect_success 'select a file' '
	but ls-tree HEAD | grep ^100644 |
	sort -k 3 | head -1 | cut -f 2 >filelist
'

file=$(cat filelist)
export file

test_perf 'but rev-list --topo-order (baseline)' '
	but rev-list --topo-order HEAD >/dev/null
'

test_perf 'but log --follow (baseline for -M)' '
	but log --oneline --follow -- "$file" >/dev/null
'

test_perf 'but log -L (renames off)' '
	but log --no-renames -L 1:"$file" >/dev/null
'

test_perf 'but log -L (renames on)' '
	but log -M -L 1:"$file" >/dev/null
'

test_perf 'but log --oneline --raw --parents' '
	but log --oneline --raw --parents >/dev/null
'

test_perf 'but log --oneline --raw --parents -1000' '
	but log --oneline --raw --parents -1000 >/dev/null
'

test_done
