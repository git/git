#!/bin/sh

test_description='check infrastructure for svn importer'

. ./test-lib.sh
uint32_max=4294967295

test_expect_success 'obj pool: store data' '
	cat <<-\EOF >expected &&
	0
	1
	EOF

	test-obj-pool <<-\EOF >actual &&
	alloc one 16
	set one 13
	test one 13
	reset one
	EOF
	test_cmp expected actual
'

test_expect_success 'obj pool: NULL is offset ~0' '
	echo "$uint32_max" >expected &&
	echo null one | test-obj-pool >actual &&
	test_cmp expected actual
'

test_expect_success 'obj pool: out-of-bounds access' '
	cat <<-EOF >expected &&
	0
	0
	$uint32_max
	$uint32_max
	16
	20
	$uint32_max
	EOF

	test-obj-pool <<-\EOF >actual &&
	alloc one 16
	alloc two 16
	offset one 20
	offset two 20
	alloc one 5
	offset one 20
	free one 1
	offset one 20
	reset one
	reset two
	EOF
	test_cmp expected actual
'

test_expect_success 'obj pool: high-water mark' '
	cat <<-\EOF >expected &&
	0
	0
	10
	20
	20
	20
	EOF

	test-obj-pool <<-\EOF >actual &&
	alloc one 10
	committed one
	alloc one 10
	commit one
	committed one
	alloc one 10
	free one 20
	committed one
	reset one
	EOF
	test_cmp expected actual
'

test_done
