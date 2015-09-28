#!/bin/sh

test_description='test disabling of local paths in clone/fetch'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"

test_expect_success 'setup repository to clone' '
	test_commit one
'

test_proto "file://" file "file://$PWD"
test_proto "path" file .

test_done
