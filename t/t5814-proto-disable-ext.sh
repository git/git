#!/bin/sh

test_description='test disabling of remote-helper paths in clone/fetch'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"

setup_ext_wrapper

test_expect_success 'setup repository to clone' '
	test_cummit one &&
	mkdir remote &&
	but init --bare remote/repo.but &&
	but push remote/repo.but HEAD
'

test_proto "remote-helper" ext "ext::fake-remote %S repo.but"

test_done
