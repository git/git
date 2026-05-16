#!/bin/sh
#
# Copyright (c) 2025
#

test_description='Test agent semantic diff'

. ./test-lib.sh

test_expect_success 'agent-diff generates JSON semantic diff' '
	git init agent-diff-repo &&
	cd agent-diff-repo &&
	echo "line one" >file &&
	git add file &&
	git commit -m "initial" &&
	echo "line two" >>file &&
	git add file &&
	git agent-commit -m "second" --trailer "Agent-Id:agent-1" &&
	git agent-diff --semantic HEAD~1 HEAD >diff.json &&
	grep "changes" diff.json &&
	grep "token_estimate" diff.json &&
	cd ..
'

test_expect_success 'agent-diff shows file granularity' '
	cd agent-diff-repo &&
	git agent-diff --semantic HEAD~1 HEAD >diff2.json &&
	grep "file" diff2.json &&
	cd ..
'

test_done
