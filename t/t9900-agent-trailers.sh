#!/bin/sh
#
# Copyright (c) 2025
#

test_description='Test agent trailer parsing and validation'

. ./test-lib.sh

test_expect_success 'agent trailers are added via git commit' '
	git init agent-trailer-repo &&
	cd agent-trailer-repo &&
	echo hello >world &&
	git add world &&
	git agent-commit -m "test" --trailer "Agent-Id:agent-1" --trailer "Agent-Confidence:0.95" &&
	git log -1 --format="%(trailers)" >trailers &&
	grep "Agent-Id: agent-1" trailers &&
	grep "Agent-Confidence: 0.95" trailers &&
	cd ..
'

test_expect_success 'agent-verify validates agent trailers' '
	cd agent-trailer-repo &&
	git agent-verify HEAD >verify.out &&
	grep "passed" verify.out &&
	cd ..
'

test_expect_success 'agent-verify fails on bad Agent-Confidence' '
	cd agent-trailer-repo &&
	git cat-file -p HEAD >commit.txt &&
	git hash-object -t commit -w --stdin <commit.txt >orig &&
	sed "s/Agent-Confidence: 0.95/Agent-Confidence: 1.5/" commit.txt >bad.txt &&
	new=$(git hash-object -t commit -w --stdin <bad.txt) &&
	git read-tree HEAD &&
	git checkout-index -f -a &&
	test_must_fail git agent-verify "$new" >verify2.out &&
	grep "false" verify2.out &&
	cd ..
'

test_done
