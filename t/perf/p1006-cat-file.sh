#!/bin/sh

test_description='Tests listing object info performance'
. ./perf-lib.sh

test_perf_large_repo

test_perf 'cat-file --batch-check' '
	git cat-file --batch-all-objects --batch-check
'

test_done
