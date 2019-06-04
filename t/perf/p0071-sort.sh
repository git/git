#!/bin/sh

test_description='Basic sort performance tests'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'setup' '
	git ls-files --stage "*.[ch]" "*.sh" |
	cut -f2 -d" " |
	git cat-file --batch >unsorted
'

test_perf 'sort(1)' '
	sort <unsorted >expect
'

test_perf 'string_list_sort()' '
	test-tool string-list sort <unsorted >actual
'

test_expect_success 'string_list_sort() sorts like sort(1)' '
	test_cmp_bin expect actual
'

test_done
