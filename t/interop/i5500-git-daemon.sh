#!/bin/sh

VERSION_A=.
VERSION_B=v1.0.0

: ${LIB_GIT_DAEMON_PORT:=5500}
LIB_GIT_DAEMON_COMMAND='git.a daemon'

test_description='clone and fetch by older client'
. ./interop-lib.sh
. "$TEST_DIRECTORY"/lib-git-daemon.sh

start_git_daemon --export-all

repo=$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo

test_expect_success "create repo served by $VERSION_A" '
	git.a init "$repo" &&
	git.a -C "$repo" commit --allow-empty -m one
'

test_expect_success "clone with $VERSION_B" '
	git.b clone "$GIT_DAEMON_URL/repo" child &&
	echo one >expect &&
	git.a -C child log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success "fetch with $VERSION_B" '
	git.a -C "$repo" commit --allow-empty -m two &&
	(
		cd child &&
		git.b fetch
	) &&
	echo two >expect &&
	git.a -C child log -1 --format=%s FETCH_HEAD >actual &&
	test_cmp expect actual
'

stop_git_daemon
test_done
