#!/bin/sh

VERSION_A=.
VERSION_B=v2.0.0

: ${LIB_GIT_DAEMON_PORT:=5700}
LIB_GIT_DAEMON_COMMAND='but.b daemon'

test_description='clone and fetch by client who is trying to use a new protocol'
. ./interop-lib.sh
. "$TEST_DIRECTORY"/lib-but-daemon.sh

start_but_daemon --export-all

repo=$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo

test_expect_success "create repo served by $VERSION_B" '
	but.b init "$repo" &&
	but.b -C "$repo" cummit --allow-empty -m one
'

test_expect_success "but:// clone with $VERSION_A and protocol v1" '
	GIT_TRACE_PACKET=1 but.a -c protocol.version=1 clone "$GIT_DAEMON_URL/repo" child 2>log &&
	but.a -C child log -1 --format=%s >actual &&
	but.b -C "$repo" log -1 --format=%s >expect &&
	test_cmp expect actual &&
	grep "version=1" log
'

test_expect_success "but:// fetch with $VERSION_A and protocol v1" '
	but.b -C "$repo" cummit --allow-empty -m two &&
	but.b -C "$repo" log -1 --format=%s >expect &&

	GIT_TRACE_PACKET=1 but.a -C child -c protocol.version=1 fetch 2>log &&
	but.a -C child log -1 --format=%s FETCH_HEAD >actual &&

	test_cmp expect actual &&
	grep "version=1" log &&
	! grep "version 1" log
'

stop_but_daemon

test_expect_success "create repo served by $VERSION_B" '
	but.b init parent &&
	but.b -C parent cummit --allow-empty -m one
'

test_expect_success "file:// clone with $VERSION_A and protocol v1" '
	GIT_TRACE_PACKET=1 but.a -c protocol.version=1 clone --upload-pack="but.b upload-pack" parent child2 2>log &&
	but.a -C child2 log -1 --format=%s >actual &&
	but.b -C parent log -1 --format=%s >expect &&
	test_cmp expect actual &&
	! grep "version 1" log
'

test_expect_success "file:// fetch with $VERSION_A and protocol v1" '
	but.b -C parent cummit --allow-empty -m two &&
	but.b -C parent log -1 --format=%s >expect &&

	GIT_TRACE_PACKET=1 but.a -C child2 -c protocol.version=1 fetch --upload-pack="but.b upload-pack" 2>log &&
	but.a -C child2 log -1 --format=%s FETCH_HEAD >actual &&

	test_cmp expect actual &&
	! grep "version 1" log
'

test_done
