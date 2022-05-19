#!/bin/sh

test_description='remote push rejects are reported by client'

. ./test-lib.sh

test_expect_success 'setup' '
	test_hook update <<-\EOF &&
	exit 1
	EOF
	echo 1 >file &&
	but add file &&
	but cummit -m 1 &&
	but clone . child &&
	cd child &&
	echo 2 >file &&
	but cummit -a -m 2
'

test_expect_success 'push reports error' 'test_must_fail but push 2>stderr'

test_expect_success 'individual ref reports error' 'grep rejected stderr'

test_done
