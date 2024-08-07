#!/bin/sh

test_description="Tests diff generation performance"

. ./perf-lib.sh

test_perf_default_repo

test_perf 'log -3000 (baseline)' '
	git log -3000 >/dev/null
'

test_perf 'log --raw -3000 (tree-only)' '
	git log --raw -3000 >/dev/null
'

test_perf 'log -p -3000 (Myers)' '
	git log -p -3000 >/dev/null
'

test_perf 'log -p -3000 --histogram' '
	git log -p -3000 --histogram >/dev/null
'

test_perf 'log -p -3000 --patience' '
	git log -p -3000 --patience >/dev/null
'

test_done
