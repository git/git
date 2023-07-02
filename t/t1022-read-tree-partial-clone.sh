#!/bin/sh

test_description='git read-tree in partial clones'

TEST_NO_CREATE_REPO=1
TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'read-tree in partial clone prefetches in one batch' '
	test_when_finished "rm -rf server client trace" &&

	git init server &&
	echo foo >server/one &&
	echo bar >server/two &&
	git -C server add one two &&
	git -C server commit -m "initial commit" &&
	TREE=$(git -C server rev-parse HEAD^{tree}) &&

	git -C server config uploadpack.allowfilter 1 &&
	git -C server config uploadpack.allowanysha1inwant 1 &&
	git clone --bare --filter=blob:none "file://$(pwd)/server" client &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client read-tree $TREE $TREE &&

	# "done" marks the end of negotiation (once per fetch). Expect that
	# only one fetch occurs.
	grep "fetch> done" trace >donelines &&
	test_line_count = 1 donelines
'

test_done
