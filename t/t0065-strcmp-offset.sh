#!/bin/sh

test_description='Test strcmp_offset functionality'

. ./test-lib.sh

test_expect_success run_helper '
	test-strcmp-offset
'

test_done
