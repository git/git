#!/bin/sh

test_description='pulling into void'

. ./test-lib.sh

D=`pwd`

test_expect_success setup '

	echo file >file &&
	git add file &&
	git commit -a -m original

'

test_expect_success 'pulling into void' '
	mkdir cloned &&
	cd cloned &&
	git init &&
	git pull ..
'

cd "$D"

test_expect_success 'checking the results' '
	test -f file &&
	test -f cloned/file &&
	diff file cloned/file
'

test_done

