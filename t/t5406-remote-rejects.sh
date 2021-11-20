#!/bin/sh

test_description='remote push rejects are reported by client'

. ./test-lib.sh

test_expect_success 'setup' '
	write_script .git/hooks/update <<-\EOF &&
	exit 1
	EOF
	echo 1 >file &&
	git add file &&
	git commit -m 1 &&
	git clone . child &&
	cd child &&
	echo 2 >file &&
	git commit -a -m 2
'

test_expect_success 'push reports error' 'test_must_fail git push 2>stderr'

test_expect_success 'individual ref reports error' 'grep rejected stderr'

test_done
