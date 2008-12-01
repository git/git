#!/bin/sh

test_description='pull options'

. ./test-lib.sh

D=`pwd`

test_expect_success 'setup' '
	mkdir parent &&
	(cd parent && git init &&
	 echo one >file && git add file &&
	 git commit -m one)
'

cd "$D"

test_expect_success 'git pull -q' '
	mkdir clonedq &&
	cd clonedq &&
	git pull -q "$D/parent" >out 2>err &&
	test ! -s out
'

cd "$D"

test_expect_success 'git pull' '
	mkdir cloned &&
	cd cloned &&
	git pull "$D/parent" >out 2>err &&
	test -s out
'
cd "$D"

test_expect_success 'git pull -v' '
	mkdir clonedv &&
	cd clonedv &&
	git pull -v "$D/parent" >out 2>err &&
	test -s out
'

cd "$D"

test_expect_success 'git pull -v -q' '
	mkdir clonedvq &&
	cd clonedvq &&
	git pull -v -q "$D/parent" >out 2>err &&
	test ! -s out
'

cd "$D"

test_expect_success 'git pull -q -v' '
	mkdir clonedqv &&
	cd clonedqv &&
	git pull -q -v "$D/parent" >out 2>err &&
	test -s out
'

test_done
