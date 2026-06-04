#!/bin/sh

# Note that this test only works on real version numbers,
# as it depends on matching the output to "git version".
VERSION_A=v1.6.6.3
VERSION_B=v2.11.1

test_description='sanity test interop library'
. ./interop-lib.sh

test_expect_success 'bare git is forbidden' '
	test_must_fail git version
'

test_expect_success "git.a version ($VERSION_A)" '
	echo git version ${VERSION_A#v} >expect &&
	git.a version >actual &&
	test_cmp expect actual
'

test_expect_success "git.b version ($VERSION_B)" '
	echo git version ${VERSION_B#v} >expect &&
	git.b version >actual &&
	test_cmp expect actual
'

test_done
