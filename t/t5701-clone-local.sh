#!/bin/sh

test_description='test local clone'
. ./test-lib.sh

D=`pwd`

test_expect_success 'preparing origin repository' '
	: >file && git add . && git commit -m1 &&
	git clone --bare . a.git &&
	git clone --bare . x
'

test_expect_success 'local clone without .git suffix' '
	cd "$D" &&
	git clone -l -s a b &&
	cd b &&
	git fetch
'

test_expect_success 'local clone with .git suffix' '
	cd "$D" &&
	git clone -l -s a.git c &&
	cd c &&
	git fetch
'

test_expect_success 'local clone from x' '
	cd "$D" &&
	git clone -l -s x y &&
	cd y &&
	git fetch
'

test_expect_success 'local clone from x.git that does not exist' '
	cd "$D" &&
	if git clone -l -s x.git z
	then
		echo "Oops, should have failed"
		false
	else
		echo happy
	fi
'

test_done
