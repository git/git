#!/bin/sh

test_description='pull options'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir parent &&
	(cd parent && git init &&
	 echo one >file && git add file &&
	 git commit -m one)
'

test_expect_success 'git pull -q' '
	mkdir clonedq &&
	(cd clonedq && git init &&
	git pull -q "../parent" >out 2>err &&
	test ! -s err &&
	test ! -s out)
'

test_expect_success 'git pull' '
	mkdir cloned &&
	(cd cloned && git init &&
	git pull "../parent" >out 2>err &&
	test -s err &&
	test ! -s out)
'

test_expect_success 'git pull -v' '
	mkdir clonedv &&
	(cd clonedv && git init &&
	git pull -v "../parent" >out 2>err &&
	test -s err &&
	test ! -s out)
'

test_expect_success 'git pull -v -q' '
	mkdir clonedvq &&
	(cd clonedvq && git init &&
	git pull -v -q "../parent" >out 2>err &&
	test ! -s out &&
	test ! -s err)
'

test_expect_success 'git pull -q -v' '
	mkdir clonedqv &&
	(cd clonedqv && git init &&
	git pull -q -v "../parent" >out 2>err &&
	test ! -s out &&
	test -s err)
'

test_done
