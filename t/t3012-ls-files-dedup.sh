#!/bin/sh

test_description='but ls-files --deduplicate test'

. ./test-lib.sh

test_expect_success 'setup' '
	>a.txt &&
	>b.txt &&
	>delete.txt &&
	but add a.txt b.txt delete.txt &&
	but cummit -m base &&
	echo a >a.txt &&
	echo b >b.txt &&
	echo delete >delete.txt &&
	but add a.txt b.txt delete.txt &&
	but cummit -m tip &&
	but tag tip &&
	but reset --hard HEAD^ &&
	echo change >a.txt &&
	but cummit -a -m side &&
	but tag side
'

test_expect_success 'but ls-files --deduplicate to show unique unmerged path' '
	test_must_fail but merge tip &&
	but ls-files --deduplicate >actual &&
	cat >expect <<-\EOF &&
	a.txt
	b.txt
	delete.txt
	EOF
	test_cmp expect actual &&
	but merge --abort
'

test_expect_success 'but ls-files -d -m --deduplicate with different display options' '
	but reset --hard side &&
	test_must_fail but merge tip &&
	rm delete.txt &&
	but ls-files -d -m --deduplicate >actual &&
	cat >expect <<-\EOF &&
	a.txt
	delete.txt
	EOF
	test_cmp expect actual &&
	but ls-files -d -m -t --deduplicate >actual &&
	cat >expect <<-\EOF &&
	C a.txt
	C a.txt
	C a.txt
	R delete.txt
	C delete.txt
	EOF
	test_cmp expect actual &&
	but ls-files -d -m -c --deduplicate >actual &&
	cat >expect <<-\EOF &&
	a.txt
	b.txt
	delete.txt
	EOF
	test_cmp expect actual &&
	but merge --abort
'

test_done
