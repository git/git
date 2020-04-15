#!/bin/sh

test_description='combined diff show only paths that are different to all parents'

. ./test-lib.sh

# verify that diffc.expect matches output of
# $(git diff -c --name-only HEAD HEAD^ HEAD^2)
diffc_verify () {
	git diff -c --name-only HEAD HEAD^ HEAD^2 >diffc.actual &&
	test_cmp diffc.expect diffc.actual
}

test_expect_success 'trivial merge - combine-diff empty' '
	for i in $(test_seq 1 9)
	do
		echo $i >$i.txt &&
		git add $i.txt
	done &&
	git commit -m "init" &&
	git checkout -b side &&
	for i in $(test_seq 2 9)
	do
		echo $i/2 >>$i.txt
	done &&
	git commit -a -m "side 2-9" &&
	git checkout master &&
	echo 1/2 >1.txt &&
	git commit -a -m "master 1" &&
	git merge side &&
	>diffc.expect &&
	diffc_verify
'


test_expect_success 'only one truly conflicting path' '
	git checkout side &&
	for i in $(test_seq 2 9)
	do
		echo $i/3 >>$i.txt
	done &&
	echo "4side" >>4.txt &&
	git commit -a -m "side 2-9 +4" &&
	git checkout master &&
	for i in $(test_seq 1 9)
	do
		echo $i/3 >>$i.txt
	done &&
	echo "4master" >>4.txt &&
	git commit -a -m "master 1-9 +4" &&
	test_must_fail git merge side &&
	cat <<-\EOF >4.txt &&
	4
	4/2
	4/3
	4master
	4side
	EOF
	git add 4.txt &&
	git commit -m "merge side (2)" &&
	echo 4.txt >diffc.expect &&
	diffc_verify
'

test_expect_success 'merge introduces new file' '
	git checkout side &&
	for i in $(test_seq 5 9)
	do
		echo $i/4 >>$i.txt
	done &&
	git commit -a -m "side 5-9" &&
	git checkout master &&
	for i in $(test_seq 1 3)
	do
		echo $i/4 >>$i.txt
	done &&
	git commit -a -m "master 1-3 +4hello" &&
	git merge side &&
	echo "Hello World" >4hello.txt &&
	git add 4hello.txt &&
	git commit --amend &&
	echo 4hello.txt >diffc.expect &&
	diffc_verify
'

test_expect_success 'merge removed a file' '
	git checkout side &&
	for i in $(test_seq 5 9)
	do
		echo $i/5 >>$i.txt
	done &&
	git commit -a -m "side 5-9" &&
	git checkout master &&
	for i in $(test_seq 1 3)
	do
		echo $i/4 >>$i.txt
	done &&
	git commit -a -m "master 1-3" &&
	git merge side &&
	git rm 4.txt &&
	git commit --amend &&
	echo 4.txt >diffc.expect &&
	diffc_verify
'

test_done
