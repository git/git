#!/bin/sh

test_description='Tests pack performance using bitmaps'
. ./perf-lib.sh

GIT_TEST_PASSING_SANITIZE_LEAK=0
export GIT_TEST_PASSING_SANITIZE_LEAK

test_perf_large_repo

test_size 'paths at head' '
	git ls-tree -r --name-only HEAD >path-list &&
	wc -l <path-list
'

test_size 'number of distinct name-hashes' '
	cat path-list | test-tool name-hash >name-hashes &&
	cat name-hashes | awk "{ print \$1; }" | sort -n | uniq -c >name-hash-count &&
	wc -l <name-hash-count
'

test_size 'number of distinct full-name-hashes' '
	cat name-hashes | awk "{ print \$2; }" | sort -n | uniq -c >full-name-hash-count &&
	wc -l <full-name-hash-count
'

test_size 'maximum multiplicity of name-hashes' '
	cat name-hash-count | \
		sort -nr | \
		head -n 1 | \
		awk "{ print \$1; }"
'

test_size 'maximum multiplicity of fullname-hashes' '
	cat full-name-hash-count | \
		sort -nr | \
		head -n 1 | \
		awk "{ print \$1; }"
'

test_done
