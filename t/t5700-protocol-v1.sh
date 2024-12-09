#!/bin/sh

test_description='test git wire-protocol transition'

TEST_NO_CREATE_REPO=1

# This is a protocol-specific test.
GIT_TEST_PROTOCOL_VERSION=0
export GIT_TEST_PROTOCOL_VERSION

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Test protocol v1 with 'git://' transport
#
. "$TEST_DIRECTORY"/lib-git-daemon.sh
start_git_daemon --export-all --enable=receive-pack
daemon_parent=$GIT_DAEMON_DOCUMENT_ROOT_PATH/parent

test_expect_success 'create repo to be served by git-daemon' '
	git init "$daemon_parent" &&
	test_commit -C "$daemon_parent" one
'

test_expect_success 'clone with git:// using protocol v1' '
	GIT_TRACE_PACKET=1 git -c protocol.version=1 \
		clone "$GIT_DAEMON_URL/parent" daemon_child 2>log &&

	git -C daemon_child log -1 --format=%s >actual &&
	git -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "clone> .*\\\0\\\0version=1\\\0$" log &&
	# Server responded using protocol v1
	grep "clone< version 1" log
'

test_expect_success 'fetch with git:// using protocol v1' '
	test_commit -C "$daemon_parent" two &&

	GIT_TRACE_PACKET=1 git -C daemon_child -c protocol.version=1 \
		fetch 2>log &&

	git -C daemon_child log -1 --format=%s origin/main >actual &&
	git -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "fetch> .*\\\0\\\0version=1\\\0$" log &&
	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'pull with git:// using protocol v1' '
	GIT_TRACE_PACKET=1 git -C daemon_child -c protocol.version=1 \
		pull 2>log &&

	git -C daemon_child log -1 --format=%s >actual &&
	git -C "$daemon_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "fetch> .*\\\0\\\0version=1\\\0$" log &&
	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'push with git:// using protocol v1' '
	test_commit -C daemon_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	GIT_TRACE_PACKET=1 git -C daemon_child -c protocol.version=1 \
		push origin HEAD:client_branch 2>log &&

	git -C daemon_child log -1 --format=%s >actual &&
	git -C "$daemon_parent" log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "push> .*\\\0\\\0version=1\\\0$" log &&
	# Server responded using protocol v1
	grep "push< version 1" log
'

stop_git_daemon

# Test protocol v1 with 'file://' transport
#
test_expect_success 'create repo to be served by file:// transport' '
	git init file_parent &&
	test_commit -C file_parent one
'

test_expect_success 'clone with file:// using protocol v1' '
	GIT_TRACE_PACKET=1 git -c protocol.version=1 \
		clone "file://$(pwd)/file_parent" file_child 2>log &&

	git -C file_child log -1 --format=%s >actual &&
	git -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "clone< version 1" log
'

test_expect_success 'fetch with file:// using protocol v1' '
	test_commit -C file_parent two &&

	GIT_TRACE_PACKET=1 git -C file_child -c protocol.version=1 \
		fetch 2>log &&

	git -C file_child log -1 --format=%s origin/main >actual &&
	git -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'pull with file:// using protocol v1' '
	GIT_TRACE_PACKET=1 git -C file_child -c protocol.version=1 \
		pull 2>log &&

	git -C file_child log -1 --format=%s >actual &&
	git -C file_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'push with file:// using protocol v1' '
	test_commit -C file_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	GIT_TRACE_PACKET=1 git -C file_child -c protocol.version=1 \
		push origin HEAD:client_branch 2>log &&

	git -C file_child log -1 --format=%s >actual &&
	git -C file_parent log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "push< version 1" log
'

test_expect_success 'cloning branchless tagless but not refless remote' '
	rm -rf server client &&

	git -c init.defaultbranch=main init server &&
	echo foo >server/foo.txt &&
	git -C server add foo.txt &&
	git -C server commit -m "message" &&
	git -C server update-ref refs/notbranch/alsonottag HEAD &&
	git -C server checkout --detach &&
	git -C server branch -D main &&
	git -C server symbolic-ref HEAD refs/heads/nonexistentbranch &&

	git -c protocol.version=1 clone "file://$(pwd)/server" client
'

# Test protocol v1 with 'ssh://' transport
#
test_expect_success 'setup ssh wrapper' '
	GIT_SSH="$GIT_BUILD_DIR/t/helper/test-fake-ssh" &&
	export GIT_SSH &&
	GIT_SSH_VARIANT=ssh &&
	export GIT_SSH_VARIANT &&
	export TRASH_DIRECTORY &&
	>"$TRASH_DIRECTORY"/ssh-output
'

expect_ssh () {
	test_when_finished '(cd "$TRASH_DIRECTORY" && rm -f ssh-expect && >ssh-output)' &&
	echo "ssh: -o SendEnv=GIT_PROTOCOL myhost $1 '$PWD/ssh_parent'" >"$TRASH_DIRECTORY/ssh-expect" &&
	(cd "$TRASH_DIRECTORY" && test_cmp ssh-expect ssh-output)
}

test_expect_success 'create repo to be served by ssh:// transport' '
	git init ssh_parent &&
	test_commit -C ssh_parent one
'

test_expect_success 'clone with ssh:// using protocol v1' '
	GIT_TRACE_PACKET=1 git -c protocol.version=1 \
		clone "ssh://myhost:$(pwd)/ssh_parent" ssh_child 2>log &&
	expect_ssh git-upload-pack &&

	git -C ssh_child log -1 --format=%s >actual &&
	git -C ssh_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "clone< version 1" log
'

test_expect_success 'fetch with ssh:// using protocol v1' '
	test_commit -C ssh_parent two &&

	GIT_TRACE_PACKET=1 git -C ssh_child -c protocol.version=1 \
		fetch 2>log &&
	expect_ssh git-upload-pack &&

	git -C ssh_child log -1 --format=%s origin/main >actual &&
	git -C ssh_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'pull with ssh:// using protocol v1' '
	GIT_TRACE_PACKET=1 git -C ssh_child -c protocol.version=1 \
		pull 2>log &&
	expect_ssh git-upload-pack &&

	git -C ssh_child log -1 --format=%s >actual &&
	git -C ssh_parent log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "fetch< version 1" log
'

test_expect_success 'push with ssh:// using protocol v1' '
	test_commit -C ssh_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	GIT_TRACE_PACKET=1 git -C ssh_child -c protocol.version=1 \
		push origin HEAD:client_branch 2>log &&
	expect_ssh git-receive-pack &&

	git -C ssh_child log -1 --format=%s >actual &&
	git -C ssh_parent log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "push< version 1" log
'

test_expect_success 'clone propagates object-format from empty repo' '
	test_when_finished "rm -fr src256 dst256" &&

	echo sha256 >expect &&
	git init --object-format=sha256 src256 &&
	git clone --no-local src256 dst256 &&
	git -C dst256 rev-parse --show-object-format >actual &&

	test_cmp expect actual
'

# Test protocol v1 with 'http://' transport
#
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'create repos to be served by http:// transport' '
	git init "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" config http.receivepack true &&
	test_commit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" one &&
	git init --object-format=sha256 "$HTTPD_DOCUMENT_ROOT_PATH/sha256" &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/sha256" config http.receivepack true
'

test_expect_success 'clone with http:// using protocol v1' '
	GIT_TRACE_PACKET=1 GIT_TRACE_CURL=1 git -c protocol.version=1 \
		clone "$HTTPD_URL/smart/http_parent" http_child 2>log &&

	git -C http_child log -1 --format=%s >actual &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "Git-Protocol: version=1" log &&
	# Server responded using protocol v1
	grep "git< version 1" log
'

test_expect_success 'clone with http:// using protocol v1 with empty SHA-256 repo' '
	GIT_TRACE_PACKET=1 GIT_TRACE_CURL=1 git -c protocol.version=1 \
		clone "$HTTPD_URL/smart/sha256" sha256 2>log &&

	echo sha256 >expect &&
	git -C sha256 rev-parse --show-object-format >actual &&
	test_cmp expect actual &&

	# Client requested to use protocol v1
	grep "Git-Protocol: version=1" log &&
	# Server responded using protocol v1
	grep "git< version 1" log
'

test_expect_success 'fetch with http:// using protocol v1' '
	test_commit -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" two &&

	GIT_TRACE_PACKET=1 git -C http_child -c protocol.version=1 \
		fetch 2>log &&

	git -C http_child log -1 --format=%s origin/main >actual &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "git< version 1" log
'

test_expect_success 'pull with http:// using protocol v1' '
	GIT_TRACE_PACKET=1 git -C http_child -c protocol.version=1 \
		pull 2>log &&

	git -C http_child log -1 --format=%s >actual &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "git< version 1" log
'

test_expect_success 'push with http:// using protocol v1' '
	test_commit -C http_child three &&

	# Push to another branch, as the target repository has the
	# main branch checked out and we cannot push into it.
	GIT_TRACE_PACKET=1 git -C http_child -c protocol.version=1 \
		push origin HEAD:client_branch && #2>log &&

	git -C http_child log -1 --format=%s >actual &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/http_parent" log -1 --format=%s client_branch >expect &&
	test_cmp expect actual &&

	# Server responded using protocol v1
	grep "git< version 1" log
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
