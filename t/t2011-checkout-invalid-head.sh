#!/bin/sh

test_description='checkout switching away from an invalid branch'

. ./test-lib.sh

test_expect_success 'setup' '
	echo hello >world &&
	git add world &&
	git commit -m initial
'

test_expect_success 'checkout should not start branch from a tree' '
	test_must_fail git checkout -b newbranch master^{tree}
'

test_expect_success 'checkout master from invalid HEAD' '
	echo 0000000000000000000000000000000000000000 >.git/HEAD &&
	git checkout master --
'

test_done
