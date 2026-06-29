#!/bin/sh
#
# Copyright (c) 2025
#

test_description='Test agent session management'

. ./test-lib.sh

test_expect_success 'agent-session creates a session' '
	git init agent-session-repo &&
	cd agent-session-repo &&
	git agent-session --start --task "session-1" >session.out &&
	grep "session-" session.out &&
	cd ..
'

test_expect_success 'agent-session log is empty for fresh session' '
	cd agent-session-repo &&
	SID=$(cat session.out) &&
	git agent-session --log --session-id "$SID" >log.out &&
	test $(wc -l <log.out) -eq 0 &&
	cd ..
'

test_expect_success 'agent-session end works' '
	cd agent-session-repo &&
	SID=$(cat session.out) &&
	git agent-session --end --session-id "$SID" &&
	cd ..
'

test_done
