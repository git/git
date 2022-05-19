#!/bin/sh

test_description='but read-tree in partial clones'

TEST_NO_CREATE_REPO=1

. ./test-lib.sh

test_expect_success 'read-tree in partial clone prefetches in one batch' '
	test_when_finished "rm -rf server client trace" &&

	but init server &&
	echo foo >server/one &&
	echo bar >server/two &&
	but -C server add one two &&
	but -C server cummit -m "initial cummit" &&
	TREE=$(but -C server rev-parse HEAD^{tree}) &&

	but -C server config uploadpack.allowfilter 1 &&
	but -C server config uploadpack.allowanysha1inwant 1 &&
	but clone --bare --filter=blob:none "file://$(pwd)/server" client &&
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client read-tree $TREE &&

	# "done" marks the end of negotiation (once per fetch). Expect that
	# only one fetch occurs.
	grep "fetch> done" trace >donelines &&
	test_line_count = 1 donelines
'

test_done
