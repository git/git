#!/bin/sh

test_description='test protocol whitelisting with submodules'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-proto-disable.sh

setup_ext_wrapper
setup_ssh_wrapper

test_expect_success 'setup repository with submodules' '
	mkdir remote &&
	but init remote/repo.but &&
	(cd remote/repo.but && test_cummit one) &&
	# submodule-add should probably trust what we feed it on the cmdline,
	# but its implementation is overly conservative.
	GIT_ALLOW_PROTOCOL=ssh but submodule add remote:repo.but ssh-module &&
	GIT_ALLOW_PROTOCOL=ext but submodule add "ext::fake-remote %S repo.but" ext-module &&
	but cummit -m "add submodules"
'

test_expect_success 'clone with recurse-submodules fails' '
	test_must_fail but clone --recurse-submodules . dst
'

test_expect_success 'setup individual updates' '
	rm -rf dst &&
	but clone . dst &&
	but -C dst submodule init
'

test_expect_success 'update of ssh allowed' '
	but -C dst submodule update ssh-module
'

test_expect_success 'update of ext not allowed' '
	test_must_fail but -C dst submodule update ext-module
'

test_expect_success 'user can override whitelist' '
	GIT_ALLOW_PROTOCOL=ext but -C dst submodule update ext-module
'

test_done
