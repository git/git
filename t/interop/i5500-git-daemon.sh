#!/bin/sh

VERSION_A=.
VERSION_B=v1.0.0

: ${LIB_BUT_DAEMON_PORT:=5500}
LIB_BUT_DAEMON_COMMAND='but.a daemon'

test_description='clone and fetch by older client'
. ./interop-lib.sh
. "$TEST_DIRECTORY"/lib-but-daemon.sh

start_but_daemon --export-all

repo=$BUT_DAEMON_DOCUMENT_ROOT_PATH/repo

test_expect_success "create repo served by $VERSION_A" '
	but.a init "$repo" &&
	but.a -C "$repo" cummit --allow-empty -m one
'

test_expect_success "clone with $VERSION_B" '
	but.b clone "$BUT_DAEMON_URL/repo" child &&
	echo one >expect &&
	but.a -C child log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_expect_success "fetch with $VERSION_B" '
	but.a -C "$repo" cummit --allow-empty -m two &&
	(
		cd child &&
		but.b fetch
	) &&
	echo two >expect &&
	but.a -C child log -1 --format=%s FETCH_HEAD >actual &&
	test_cmp expect actual
'

test_done
