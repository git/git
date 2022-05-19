#!/bin/sh

test_description='test but wire-protocol transition'

TEST_NO_CREATE_REPO=1

# This is a protocol-specific test.
BUT_TEST_PROTOCOL_VERSION=0
export BUT_TEST_PROTOCOL_VERSION

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Test protocol v1 with 'but://' transport
#
. "$TEST_DIRECTORY"/lib-but-daemon.sh
start_but_daemon --export-all --enable=receive-pack
daemon_parent=$BUT_DAEMON_DOCUMENT_ROOT_PATH/parent

test_expect_success 'create repo to be served by but-daemon' '
	but init "$daemon_parent" &&
	test_cummit -C "$daemon_parent" one
'

test_expect_success 'clone with but:// using protocol v1' '
	BUT_TRACE_PACKET=1 but -c protocol.version=1 \
		clone "$BUT_DAEMON_URL/parent" daemon_child 2>log &&

	but -C daemon_child log -1 --format=%s >actual &&
	but -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "clone> .*\\\0\\\0version=1\\\0$" log &&
	# Server responded using protocol v1
	grep "clone< version 1" log
'

test_expect_success 'fetch with but:// using protocol v1' '
	test_cummit -C "$daemon_parent" two &&

	BUT_TRACE_PACKET=1 but -C daemon_child -c protocol.version=1 \
		fetch 2>log &&

	but -C daemon_child log -1 --format=%s origin/main >actual &&
	but -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "fetch> .*\\\0\\\0version=1\\\0$" log &&
	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'pull with but:// using protocol v1' '
	BUT_TRACE_PACKET=1 but -C daemon_child -c protocol.version=1 \
		pull 2>log &&

	but -C daemon_child log -1 --format=%s >actual &&
	but -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "fetch> .*\\\0\\\0version=1\\\0$" log &&
	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'push with but:// using protocol v1' '
	test_cummit -C daemon_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	BUT_TRACE_PACKET=1 but -C daemon_child -c protocol.version=1 \
		push origin HEAD:client_branch 2>log &&

	but -C daemon_child log -1 --format=%s >actual &&
	but -C "$daemon_parent" log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "push> .*\\\0\\\0version=1\\\0$" log &&
	# Server responded using protocol v1
	grep "push< version 1" log
'

stop_but_daemon

# Test protocol v1 with 'file://' transport
#
test_expect_success 'create repo to be served by file:// transport' '
	but init file_parent &&
	test_cummit -C file_parent one
'

test_expect_success 'clone with file:// using protocol v1' '
	BUT_TRACE_PACKET=1 but -c protocol.version=1 \
		clone "file://$(pwd)/file_parent" file_child 2>log &&

	but -C file_child log -1 --format=%s >actual &&
	but -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "clone< version 1" log
'

test_expect_success 'fetch with file:// using protocol v1' '
	test_cummit -C file_parent two &&

	BUT_TRACE_PACKET=1 but -C file_child -c protocol.version=1 \
		fetch 2>log &&

	but -C file_child log -1 --format=%s origin/main >actual &&
	but -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'pull with file:// using protocol v1' '
	BUT_TRACE_PACKET=1 but -C file_child -c protocol.version=1 \
		pull 2>log &&

	but -C file_child log -1 --format=%s >actual &&
	but -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'push with file:// using protocol v1' '
	test_cummit -C file_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	BUT_TRACE_PACKET=1 but -C file_child -c protocol.version=1 \
		push origin HEAD:client_branch 2>log &&

	but -C file_child log -1 --format=%s >actual &&
	but -C file_parent log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "push< version 1" log
'

test_expect_success 'cloning branchless tagless but not refless remote' '
	rm -rf server client &&

	but -c init.defaultbranch=main init server &&
	echo foo >server/foo.txt &&
	but -C server add foo.txt &&
	but -C server cummit -m "message" &&
	but -C server update-ref refs/notbranch/alsonottag HEAD &&
	but -C server checkout --detach &&
	but -C server branch -D main &&
	but -C server symbolic-ref HEAD refs/heads/nonexistentbranch &&

	but -c protocol.version=1 clone "file://$(pwd)/server" client
'

# Test protocol v1 with 'ssh://' transport
#
test_expect_success 'setup ssh wrapper' '
	BUT_SSH="$BUT_BUILD_DIR/t/helper/test-fake-ssh" &&
	export BUT_SSH &&
	BUT_SSH_VARIANT=ssh &&
	export BUT_SSH_VARIANT &&
	export TRASH_DIRECTORY &&
	>"$TRASH_DIRECTORY"/ssh-output
'

expect_ssh () {
	test_when_finished '(cd "$TRASH_DIRECTORY" && rm -f ssh-expect && >ssh-output)' &&
	echo "ssh: -o SendEnv=BUT_PROTOCOL myhost $1 '$PWD/ssh_parent'" >"$TRASH_DIRECTORY/ssh-expect" &&
	(cd "$TRASH_DIRECTORY" && test_cmp ssh-expect ssh-output)
}

test_expect_success 'create repo to be served by ssh:// transport' '
	but init ssh_parent &&
	test_cummit -C ssh_parent one
'

test_expect_success 'clone with ssh:// using protocol v1' '
	BUT_TRACE_PACKET=1 but -c protocol.version=1 \
		clone "ssh://myhost:$(pwd)/ssh_parent" ssh_child 2>log &&
	expect_ssh but-upload-pack &&

	but -C ssh_child log -1 --format=%s >actual &&
	but -C ssh_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "clone< version 1" log
'

test_expect_success 'fetch with ssh:// using protocol v1' '
	test_cummit -C ssh_parent two &&

	BUT_TRACE_PACKET=1 but -C ssh_child -c protocol.version=1 \
		fetch 2>log &&
	expect_ssh but-upload-pack &&

	but -C ssh_child log -1 --format=%s origin/main >actual &&
	but -C ssh_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'pull with ssh:// using protocol v1' '
	BUT_TRACE_PACKET=1 but -C ssh_child -c protocol.version=1 \
		pull 2>log &&
	expect_ssh but-upload-pack &&

	but -C ssh_child log -1 --format=%s >actual &&
	but -C ssh_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'push with ssh:// using protocol v1' '
	test_cummit -C ssh_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	BUT_TRACE_PACKET=1 but -C ssh_child -c protocol.version=1 \
		push origin HEAD:client_branch 2>log &&
	expect_ssh but-receive-pack &&

	but -C ssh_child log -1 --format=%s >actual &&
	but -C ssh_parent log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "push< version 1" log
'

# Test protocol v1 with 'http://' transport
#
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'create repo to be served by http:// transport' '
	but init "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" config http.receivepack true &&
	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" one
'

test_expect_success 'clone with http:// using protocol v1' '
	BUT_TRACE_PACKET=1 BUT_TRACE_CURL=1 but -c protocol.version=1 \
		clone "$HTTPD_URL/smart/http_parent" http_child 2>log &&

	but -C http_child log -1 --format=%s >actual &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "Git-Protocol: version=1" log &&
	# Server responded using protocol v1
	grep "but< version 1" log
'

test_expect_success 'fetch with http:// using protocol v1' '
	test_cummit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" two &&

	BUT_TRACE_PACKET=1 but -C http_child -c protocol.version=1 \
		fetch 2>log &&

	but -C http_child log -1 --format=%s origin/main >actual &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "but< version 1" log
'

test_expect_success 'pull with http:// using protocol v1' '
	BUT_TRACE_PACKET=1 but -C http_child -c protocol.version=1 \
		pull 2>log &&

	but -C http_child log -1 --format=%s >actual &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "but< version 1" log
'

test_expect_success 'push with http:// using protocol v1' '
	test_cummit -C http_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	BUT_TRACE_PACKET=1 but -C http_child -c protocol.version=1 \
		push origin HEAD:client_branch && #2>log &&

	but -C http_child log -1 --format=%s >actual &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "but< version 1" log
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
