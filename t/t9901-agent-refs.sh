#!/bin/sh
#
# Copyright (c) 2025
#

test_description='Test agent annotation ref store'

. ./test-lib.sh

test_expect_success 'agent-commit writes reasoning annotation' '
	git init agent-refs-repo &&
	cd agent-refs-repo &&
	echo hello >world &&
	git add world &&
	git agent-commit -m "test" --reasoning "planned carefully" &&
	git agent-log --reasoning HEAD >reasoning.out &&
	grep "planned carefully" reasoning.out &&
	cd ..
'

test_expect_success 'agent-commit plan is empty when not set' '
	cd agent-refs-repo &&
	git agent-log --plan HEAD >plan.out &&
	test $(wc -l <plan.out) -eq 1 &&
	cd ..
'

test_expect_success 'refs/agent/commits ref exists after commit' '
	cd agent-refs-repo &&
	test -f .git/refs/agent/commits &&
	cd ..
'

test_done
