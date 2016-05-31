#!/bin/sh

test_description='test disabling of git-over-tcp in clone/fetch'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"
. "$TEST_DIRECTORY/lib-git-daemon.sh"
start_git_daemon

test_expect_success 'create git-accessible repo' '
	bare="$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.git" &&
	test_commit one &&
	git --bare init "$bare" &&
	git push "$bare" HEAD &&
	>"$bare/git-daemon-export-ok" &&
	git -C "$bare" config daemon.receivepack true
'

test_proto "git://" git "$GIT_DAEMON_URL/repo.git"

test_done
