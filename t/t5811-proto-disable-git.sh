#!/bin/sh

test_description='test disabling of but-over-tcp in clone/fetch'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"
. "$TEST_DIRECTORY/lib-but-daemon.sh"
start_but_daemon

test_expect_success 'create but-accessible repo' '
	bare="$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.but" &&
	test_cummit one &&
	but --bare init "$bare" &&
	but push "$bare" HEAD &&
	>"$bare/but-daemon-export-ok" &&
	but -C "$bare" config daemon.receivepack true
'

test_proto "but://" but "$GIT_DAEMON_URL/repo.but"

test_done
