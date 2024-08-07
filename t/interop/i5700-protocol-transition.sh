#!/bin/sh

VERSION_A=.
VERSION_B=v2.0.0

: ${LIB_GIT_DAEMON_PORT:=5700}
LIB_GIT_DAEMON_COMMAND='git.b daemon'

test_description='clone and fetch by client who is trying to use a new protocol'
. ./interop-lib.sh
. "$TEST_DIRECTORY"/lib-git-daemon.sh

start_git_daemon --export-all

repo=$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo

test_expect_success "create repo served by $VERSION_B" '
	git.b init "$repo" &&
	git.b -C "$repo" commit --allow-empty -m one
'

test_expect_success "git:// clone with $VERSION_A and protocol v1" '
	GIT_TRACE_PACKET=1 git.a -c protocol.version=1 clone "$GIT_DAEMON_URL/repo" child 2>log &&
	git.a -C child log -1 --format=%s >actual &&
	git.b -C "$repo" log -1 --format=%s >expect &&
	test_cmp expect actual &&
	grep "version=1" log
'

test_expect_success "git:// fetch with $VERSION_A and protocol v1" '
	git.b -C "$repo" commit --allow-empty -m two &&
	git.b -C "$repo" log -1 --format=%s >expect &&

	GIT_TRACE_PACKET=1 git.a -C child -c protocol.version=1 fetch 2>log &&
	git.a -C child log -1 --format=%s FETCH_HEAD >actual &&

	test_cmp expect actual &&
	grep "version=1" log &&
	! grep "version 1" log
'

stop_git_daemon

test_expect_success "create repo served by $VERSION_B" '
	git.b init parent &&
	git.b -C parent commit --allow-empty -m one
'

test_expect_success "file:// clone with $VERSION_A and protocol v1" '
	GIT_TRACE_PACKET=1 git.a -c protocol.version=1 clone --upload-pack="git.b upload-pack" parent child2 2>log &&
	git.a -C child2 log -1 --format=%s >actual &&
	git.b -C parent log -1 --format=%s >expect &&
	test_cmp expect actual &&
	! grep "version 1" log
'

test_expect_success "file:// fetch with $VERSION_A and protocol v1" '
	git.b -C parent commit --allow-empty -m two &&
	git.b -C parent log -1 --format=%s >expect &&

	GIT_TRACE_PACKET=1 git.a -C child2 -c protocol.version=1 fetch --upload-pack="git.b upload-pack" 2>log &&
	git.a -C child2 log -1 --format=%s FETCH_HEAD >actual &&

	test_cmp expect actual &&
	! grep "version 1" log
'

test_done
