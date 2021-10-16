#!/bin/sh

test_description='basic tests for the oid array implementation'
. ./test-lib.sh

echoid () {
	prefix="${1:+$1 }"
	shift
	while test $# -gt 0
	do
		echo "$prefix$ZERO_OID" | sed -e "s/00/$1/g"
		shift
	done
}

test_expect_success 'ordered enumeration' '
	echoid "" 44 55 88 aa >expect &&
	{
		echoid append 88 44 aa 55 &&
		echo for_each_unique
	} | test-tool oid-array >actual &&
	test_cmp expect actual
'

test_expect_success 'ordered enumeration with duplicate suppression' '
	echoid "" 44 55 88 aa >expect &&
	{
		echoid append 88 44 aa 55 &&
		echoid append 88 44 aa 55 &&
		echoid append 88 44 aa 55 &&
		echo for_each_unique
	} | test-tool oid-array >actual &&
	test_cmp expect actual
'

test_expect_success 'lookup' '
	{
		echoid append 88 44 aa 55 &&
		echoid lookup 55
	} | test-tool oid-array >actual &&
	n=$(cat actual) &&
	test "$n" -eq 1
'

test_expect_success 'lookup non-existing entry' '
	{
		echoid append 88 44 aa 55 &&
		echoid lookup 33
	} | test-tool oid-array >actual &&
	n=$(cat actual) &&
	test "$n" -lt 0
'

test_expect_success 'lookup with duplicates' '
	{
		echoid append 88 44 aa 55 &&
		echoid append 88 44 aa 55 &&
		echoid append 88 44 aa 55 &&
		echoid lookup 55
	} | test-tool oid-array >actual &&
	n=$(cat actual) &&
	test "$n" -ge 3 &&
	test "$n" -le 5
'

test_expect_success 'lookup non-existing entry with duplicates' '
	{
		echoid append 88 44 aa 55 &&
		echoid append 88 44 aa 55 &&
		echoid append 88 44 aa 55 &&
		echoid lookup 66
	} | test-tool oid-array >actual &&
	n=$(cat actual) &&
	test "$n" -lt 0
'

test_expect_success 'lookup with almost duplicate values' '
	# n-1 5s
	root=$(echoid "" 55) &&
	root=${root%5} &&
	{
		id1="${root}5" &&
		id2="${root}f" &&
		echo "append $id1" &&
		echo "append $id2" &&
		echoid lookup 55
	} | test-tool oid-array >actual &&
	n=$(cat actual) &&
	test "$n" -eq 0
'

test_expect_success 'lookup with single duplicate value' '
	{
		echoid append 55 55 &&
		echoid lookup 55
	} | test-tool oid-array >actual &&
	n=$(cat actual) &&
	test "$n" -ge 0 &&
	test "$n" -le 1
'

test_done
