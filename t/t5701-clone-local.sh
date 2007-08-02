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

test_expect_success 'With -no-hardlinks, local will make a copy' '
	cd "$D" &&
	git clone --bare --no-hardlinks x w &&
	cd w &&
	linked=$(find objects -type f ! -links 1 | wc -l) &&
	test "$linked" = 0
'

test_expect_success 'Even without -l, local will make a hardlink' '
	cd "$D" &&
	rm -fr w &&
	git clone -l --bare x w &&
	cd w &&
	copied=$(find objects -type f -links 1 | wc -l) &&
	test "$copied" = 0
'

test_done
