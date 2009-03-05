#!/bin/sh

test_description='basic symbolic-ref tests'
. ./test-lib.sh

# If the tests munging HEAD fail, they can break detection of
# the git repo, meaning that further tests will operate on
# the surrounding git repo instead of the trash directory.
reset_to_sane() {
	echo ref: refs/heads/foo >.git/HEAD
}

test_expect_success 'symbolic-ref writes HEAD' '
	git symbolic-ref HEAD refs/heads/foo &&
	echo ref: refs/heads/foo >expect &&
	test_cmp expect .git/HEAD
'

test_expect_success 'symbolic-ref reads HEAD' '
	echo refs/heads/foo >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref refuses non-ref for HEAD' '
	test_must_fail git symbolic-ref HEAD foo
'
reset_to_sane

test_expect_success 'symbolic-ref refuses bare sha1' '
	echo content >file && git add file && git commit -m one
	test_must_fail git symbolic-ref HEAD `git rev-parse HEAD`
'
reset_to_sane

test_done
