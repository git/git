#!/bin/sh

test_description='tests for test-tool chmtime'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	echo hello >file
'

test_expect_success 'mutually exclusive options are rejected' '
	test_must_fail test-tool chmtime --get --verbose +0 file 2>err &&
	test_grep "cannot be used together" err
'

test_expect_success 'missing timespec without -g or -v is rejected' '
	test_must_fail test-tool chmtime file 2>err &&
	test_grep "usage: test-tool chmtime" err
'

test_expect_success 'negative offset is not taken as an option' '
	test-tool chmtime =1000000000 file &&
	test-tool chmtime --get -86400 file >actual &&
	echo 999913600 >expect &&
	test_cmp expect actual
'

test_done
