#!/bin/sh

test_description='combined diff show only paths that are different to all parents'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# verify that diffc.expect matches output of
# $(but diff -c --name-only HEAD HEAD^ HEAD^2)
diffc_verify () {
	but diff -c --name-only HEAD HEAD^ HEAD^2 >diffc.actual &&
	test_cmp diffc.expect diffc.actual
}

test_expect_success 'trivial merge - combine-diff empty' '
	for i in $(test_seq 1 9)
	do
		echo $i >$i.txt &&
		but add $i.txt || return 1
	done &&
	but cummit -m "init" &&
	but checkout -b side &&
	for i in $(test_seq 2 9)
	do
		echo $i/2 >>$i.txt || return 1
	done &&
	but cummit -a -m "side 2-9" &&
	but checkout main &&
	echo 1/2 >1.txt &&
	but cummit -a -m "main 1" &&
	but merge side &&
	>diffc.expect &&
	diffc_verify
'


test_expect_success 'only one truly conflicting path' '
	but checkout side &&
	for i in $(test_seq 2 9)
	do
		echo $i/3 >>$i.txt || return 1
	done &&
	echo "4side" >>4.txt &&
	but cummit -a -m "side 2-9 +4" &&
	but checkout main &&
	for i in $(test_seq 1 9)
	do
		echo $i/3 >>$i.txt || return 1
	done &&
	echo "4main" >>4.txt &&
	but cummit -a -m "main 1-9 +4" &&
	test_must_fail but merge side &&
	cat <<-\EOF >4.txt &&
	4
	4/2
	4/3
	4main
	4side
	EOF
	but add 4.txt &&
	but cummit -m "merge side (2)" &&
	echo 4.txt >diffc.expect &&
	diffc_verify
'

test_expect_success 'merge introduces new file' '
	but checkout side &&
	for i in $(test_seq 5 9)
	do
		echo $i/4 >>$i.txt || return 1
	done &&
	but cummit -a -m "side 5-9" &&
	but checkout main &&
	for i in $(test_seq 1 3)
	do
		echo $i/4 >>$i.txt || return 1
	done &&
	but cummit -a -m "main 1-3 +4hello" &&
	but merge side &&
	echo "Hello World" >4hello.txt &&
	but add 4hello.txt &&
	but cummit --amend &&
	echo 4hello.txt >diffc.expect &&
	diffc_verify
'

test_expect_success 'merge removed a file' '
	but checkout side &&
	for i in $(test_seq 5 9)
	do
		echo $i/5 >>$i.txt || return 1
	done &&
	but cummit -a -m "side 5-9" &&
	but checkout main &&
	for i in $(test_seq 1 3)
	do
		echo $i/4 >>$i.txt || return 1
	done &&
	but cummit -a -m "main 1-3" &&
	but merge side &&
	but rm 4.txt &&
	but cummit --amend &&
	echo 4.txt >diffc.expect &&
	diffc_verify
'

test_done
