#!/bin/sh

test_description='basic tests for the SHA1 array implementation'
. ./test-lib.sh

echo20 () {
	prefix="${1:+$1 }"
	shift
	while test $# -gt 0
	do
		echo "$prefix$1$1$1$1$1$1$1$1$1$1$1$1$1$1$1$1$1$1$1$1"
		shift
	done
}

test_expect_success 'ordered enumeration' '
	echo20 "" 44 55 88 aa >expect &&
	{
		echo20 append 88 44 aa 55 &&
		echo for_each_unique
	} | test-sha1-array >actual &&
	test_cmp expect actual
'

test_expect_success 'ordered enumeration with duplicate suppression' '
	echo20 "" 44 55 88 aa >expect &&
	{
		echo20 append 88 44 aa 55 &&
		echo20 append 88 44 aa 55 &&
		echo for_each_unique
	} | test-sha1-array >actual &&
	test_cmp expect actual
'

test_expect_success 'lookup' '
	{
		echo20 append 88 44 aa 55 &&
		echo20 lookup 55
	} | test-sha1-array >actual &&
	n=$(cat actual) &&
	test "$n" -eq 1
'

test_expect_success 'lookup non-existing entry' '
	{
		echo20 append 88 44 aa 55 &&
		echo20 lookup 33
	} | test-sha1-array >actual &&
	n=$(cat actual) &&
	test "$n" -lt 0
'

test_expect_success 'lookup with duplicates' '
	{
		echo20 append 88 44 aa 55 &&
		echo20 append 88 44 aa 55 &&
		echo20 lookup 55
	} | test-sha1-array >actual &&
	n=$(cat actual) &&
	test "$n" -ge 2 &&
	test "$n" -le 3
'

test_expect_success 'lookup non-existing entry with duplicates' '
	{
		echo20 append 88 44 aa 55 &&
		echo20 append 88 44 aa 55 &&
		echo20 lookup 66
	} | test-sha1-array >actual &&
	n=$(cat actual) &&
	test "$n" -lt 0
'

test_expect_success 'lookup with almost duplicate values' '
	{
		echo "append 5555555555555555555555555555555555555555" &&
		echo "append 555555555555555555555555555555555555555f" &&
		echo20 lookup 55
	} | test-sha1-array >actual &&
	n=$(cat actual) &&
	test "$n" -eq 0
'

test_expect_success 'lookup with single duplicate value' '
	{
		echo20 append 55 55 &&
		echo20 lookup 55
	} | test-sha1-array >actual &&
	n=$(cat actual) &&
	test "$n" -ge 0 &&
	test "$n" -le 1
'

test_done
