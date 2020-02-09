#!/bin/sh

test_description='test skipping fetch negotiator'
. ./test-lib.sh

have_sent () {
	while test "$#" -ne 0
	do
		grep "fetch> have $(git -C client rev-parse $1)" trace
		if test $? -ne 0
		then
			echo "No have $(git -C client rev-parse $1) ($1)"
			return 1
		fi
		shift
	done
}

have_not_sent () {
	while test "$#" -ne 0
	do
		grep "fetch> have $(git -C client rev-parse $1)" trace
		if test $? -eq 0
		then
			return 1
		fi
		shift
	done
}

# trace_fetch <client_dir> <server_dir> [args]
#
# Trace the packet output of fetch, but make sure we disable the variable
# in the child upload-pack, so we don't combine the results in the same file.
trace_fetch () {
	client=$1; shift
	server=$1; shift
	GIT_TRACE_PACKET="$(pwd)/trace" \
	git -C "$client" fetch \
	  --upload-pack 'unset GIT_TRACE_PACKET; git-upload-pack' \
	  "$server" "$@"
}

test_expect_success 'commits with no parents are sent regardless of skip distance' '
	git init server &&
	test_commit -C server to_fetch &&

	git init client &&
	for i in $(test_seq 7)
	do
		test_commit -C client c$i
	done &&

	# We send: "c7" (skip 1) "c5" (skip 2) "c2" (skip 4). After that, since
	# "c1" has no parent, it is still sent as "have" even though it would
	# normally be skipped.
	test_config -C client fetch.negotiationalgorithm skipping &&
	trace_fetch client "$(pwd)/server" &&
	have_sent c7 c5 c2 c1 &&
	have_not_sent c6 c4 c3
'

test_expect_success 'when two skips collide, favor the larger one' '
	rm -rf server client trace &&
	git init server &&
	test_commit -C server to_fetch &&

	git init client &&
	for i in $(test_seq 11)
	do
		test_commit -C client c$i
	done &&
	git -C client checkout c5 &&
	test_commit -C client c5side &&

	# Before reaching c5, we send "c5side" (skip 1) and "c11" (skip 1) "c9"
	# (skip 2) "c6" (skip 4). The larger skip (skip 4) takes precedence, so
	# the next "have" sent will be "c1" (from "c6" skip 4) and not "c4"
	# (from "c5side" skip 1).
	test_config -C client fetch.negotiationalgorithm skipping &&
	trace_fetch client "$(pwd)/server" &&
	have_sent c5side c11 c9 c6 c1 &&
	have_not_sent c10 c8 c7 c5 c4 c3 c2
'

test_expect_success 'use ref advertisement to filter out commits' '
	rm -rf server client trace &&
	git init server &&
	test_commit -C server c1 &&
	test_commit -C server c2 &&
	test_commit -C server c3 &&
	git -C server tag -d c1 c2 c3 &&

	git clone server client &&
	test_commit -C client c4 &&
	test_commit -C client c5 &&
	git -C client checkout c4^^ &&
	test_commit -C client c2side &&

	git -C server checkout --orphan anotherbranch &&
	test_commit -C server to_fetch &&

	# The server advertising "c3" (as "refs/heads/master") means that we do
	# not need to send any ancestors of "c3", but we still need to send "c3"
	# itself.
	test_config -C client fetch.negotiationalgorithm skipping &&

	# The ref advertisement itself is filtered when protocol v2 is used, so
	# use v0.
	(
		GIT_TEST_PROTOCOL_VERSION=0 &&
		export GIT_TEST_PROTOCOL_VERSION &&
		trace_fetch client origin to_fetch
	) &&
	have_sent c5 c4^ c2side &&
	have_not_sent c4 c4^^ c4^^^
'

test_expect_success 'handle clock skew' '
	rm -rf server client trace &&
	git init server &&
	test_commit -C server to_fetch &&

	git init client &&

	# 2 regular commits
	test_tick=2000000000 &&
	test_commit -C client c1 &&
	test_commit -C client c2 &&

	# 4 old commits
	test_tick=1000000000 &&
	git -C client checkout c1 &&
	test_commit -C client old1 &&
	test_commit -C client old2 &&
	test_commit -C client old3 &&
	test_commit -C client old4 &&

	# "c2" and "c1" are popped first, then "old4" to "old1". "old1" would
	# normally be skipped, but is treated as a commit without a parent here
	# and sent, because (due to clock skew) its only parent has already been
	# popped off the priority queue.
	test_config -C client fetch.negotiationalgorithm skipping &&
	trace_fetch client "$(pwd)/server" &&
	have_sent c2 c1 old4 old2 old1 &&
	have_not_sent old3
'

test_expect_success 'do not send "have" with ancestors of commits that server ACKed' '
	rm -rf server client trace &&
	git init server &&
	test_commit -C server to_fetch &&

	git init client &&
	for i in $(test_seq 8)
	do
		git -C client checkout --orphan b$i &&
		test_commit -C client b$i.c0
	done &&
	for j in $(test_seq 19)
	do
		for i in $(test_seq 8)
		do
			git -C client checkout b$i &&
			test_commit -C client b$i.c$j
		done
	done &&

	# Copy this branch over to the server and add a commit on it so that it
	# is reachable but not advertised.
	git -C server fetch --no-tags "$(pwd)/client" b1:refs/heads/b1 &&
	git -C server checkout b1 &&
	test_commit -C server commit-on-b1 &&

	test_config -C client fetch.negotiationalgorithm skipping &&

	# NEEDSWORK: The number of "have"s sent depends on whether the transport
	# is stateful. If the overspecification of the result were reduced, this
	# test could be used for both stateful and stateless transports.
	(
		# Force protocol v0, in which local transport is stateful (in
		# protocol v2 it is stateless).
		GIT_TEST_PROTOCOL_VERSION=0 &&
		export GIT_TEST_PROTOCOL_VERSION &&
		trace_fetch client "$(pwd)/server" to_fetch
	) &&
	grep "  fetch" trace &&

	# fetch-pack sends 2 requests each containing 16 "have" lines before
	# processing the first response. In these 2 requests, 4 commits from
	# each branch are sent. Just check the first branch.
	have_sent b1.c19 b1.c17 b1.c14 b1.c9 &&
	have_not_sent b1.c18 b1.c16 b1.c15 b1.c13 b1.c12 b1.c11 b1.c10 &&

	# While fetch-pack is processing the first response, it should read that
	# the server ACKs b1.c19 and b1.c17.
	grep "fetch< ACK $(git -C client rev-parse b1.c19) common" trace &&
	grep "fetch< ACK $(git -C client rev-parse b1.c17) common" trace &&

	# fetch-pack should thus not send any more commits in the b1 branch, but
	# should still send the others (in this test, just check b2).
	for i in $(test_seq 0 8)
	do
		have_not_sent b1.c$i
	done &&
	have_sent b2.c1 b2.c0
'

test_done
