#!/bin/sh
#
# Copyright (c) 2025
#

test_description='Test agent plumbing commands integration'

. ./test-lib.sh

test_expect_success 'agent-orient shows repo summary' '
	git init agent-cmds-repo &&
	cd agent-cmds-repo &&
	git agent-orient >orient.txt &&
	grep "REPO:" orient.txt &&
	grep "OPEN-BRANCHES:" orient.txt &&
	cd ..
'

test_expect_success 'agent-log with agent commits' '
	cd agent-cmds-repo &&
	echo a >a &&
	git add a &&
	git agent-commit -m "agent change" --trailer "Agent-Id:agent-1" --trailer "Agent-Task:fix-bug" &&
	git agent-log HEAD -1 >log.out &&
	grep "agent-1" log.out &&
	grep "fix-bug" log.out &&
	cd ..
'

test_expect_success 'agent-verify on clean agent commit' '
	cd agent-cmds-repo &&
	git agent-verify HEAD >verify.out &&
	grep "passed" verify.out &&
	cd ..
'

test_done
