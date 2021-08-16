#!/bin/sh

test_description="Tests performance of reading the index"

. ./perf-lib.sh

test_perf_default_repo

count=1000
test_perf "read_cache/discard_cache $count times" "
	test-tool read-cache-perf --count=$count
"

count=100
test_perf "refresh_index() $count times" "
	test-tool read-cache-perf --count=$count --refresh
"

test_done
