#!/bin/sh

# Note that this test only works on real version numbers,
# as it depends on matching the output to "but version".
VERSION_A=v1.6.6.3
VERSION_B=v2.11.1

test_description='sanity test interop library'
. ./interop-lib.sh

test_expect_success 'bare but is forbidden' '
	test_must_fail but version
'

test_expect_success "but.a version ($VERSION_A)" '
	echo but version ${VERSION_A#v} >expect &&
	but.a version >actual &&
	test_cmp expect actual
'

test_expect_success "but.b version ($VERSION_B)" '
	echo but version ${VERSION_B#v} >expect &&
	but.b version >actual &&
	test_cmp expect actual
'

test_done
