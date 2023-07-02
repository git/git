#!/bin/sh

test_description='Basic sort performance tests'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'setup' '
	git ls-files --stage "*.[ch]" "*.sh" |
	cut -f2 -d" " |
	git cat-file --batch >unsorted
'

test_perf 'sort(1) unsorted' '
	sort <unsorted >sorted
'

test_expect_success 'reverse' '
	sort -r <unsorted >reversed
'

for file in sorted reversed
do
	test_perf "sort(1) $file" "
		sort <$file >actual
	"
done

for file in unsorted sorted reversed
do

	test_perf "string_list_sort() $file" "
		test-tool string-list sort <$file >actual
	"

	test_expect_success "string_list_sort() $file sorts like sort(1)" "
		test_cmp_bin sorted actual
	"
done

for file in unsorted sorted reversed
do
	test_perf "DEFINE_LIST_SORT $file" "
		test-tool mergesort sort <$file >actual
	"

	test_expect_success "DEFINE_LIST_SORT $file sorts like sort(1)" "
		test_cmp_bin sorted actual
	"
done

test_done
