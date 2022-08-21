#!/bin/sh

test_description='Test fsck performance'

. ./perf-lib.sh

test_perf_large_repo

test_perf 'fsck' '
	git fsck
'

test_done
