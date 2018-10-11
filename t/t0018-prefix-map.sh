#!/bin/sh

test_description='basic tests for prefix map'
. ./test-lib.sh

test_expect_success 'prefix map' '
	test-tool prefix-map
'

test_done
