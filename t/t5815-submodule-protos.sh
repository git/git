#!/bin/sh

test_description='test protocol whitelisting with submodules'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-proto-disable.sh

setup_ext_wrapper
setup_ssh_wrapper

test_expect_success 'setup repository with submodules' '
	mkdir remote &&
	git init remote/repo.git &&
	(cd remote/repo.git && test_commit one) &&
	# submodule-add should probably trust what we feed it on the cmdline,
	# but its implementation is overly conservative.
	GIT_ALLOW_PROTOCOL=ssh git submodule add remote:repo.git ssh-module &&
	GIT_ALLOW_PROTOCOL=ext git submodule add "ext::fake-remote %S repo.git" ext-module &&
	git commit -m "add submodules"
'

test_expect_success 'clone with recurse-submodules fails' '
	test_must_fail git clone --recurse-submodules . dst
'

test_expect_success 'setup individual updates' '
	rm -rf dst &&
	git clone . dst &&
	git -C dst submodule init
'

test_expect_success 'update of ssh allowed' '
	git -C dst submodule update ssh-module
'

test_expect_success 'update of ext not allowed' '
	test_must_fail git -C dst submodule update ext-module
'

test_expect_success 'user can override whitelist' '
	GIT_ALLOW_PROTOCOL=ext git -C dst submodule update ext-module
'

test_done
