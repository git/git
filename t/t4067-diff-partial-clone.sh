#!/bin/sh

test_description='behavior of diff when reading objects in a partial clone'

. ./test-lib.sh

test_expect_success 'but show batches blobs' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	echo b >server/b &&
	but -C server add a b &&
	but -C server cummit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	but clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	# Ensure that there is exactly 1 negotiation by checking that there is
	# only 1 "done" line sent. ("done" marks the end of negotiation.)
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client show HEAD &&
	grep "fetch> done" trace >done_lines &&
	test_line_count = 1 done_lines
'

test_expect_success 'diff batches blobs' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	echo b >server/b &&
	but -C server add a b &&
	but -C server cummit -m x &&
	echo c >server/c &&
	echo d >server/d &&
	but -C server add c d &&
	but -C server cummit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	but clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	# Ensure that there is exactly 1 negotiation by checking that there is
	# only 1 "done" line sent. ("done" marks the end of negotiation.)
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client diff HEAD^ HEAD &&
	grep "fetch> done" trace >done_lines &&
	test_line_count = 1 done_lines
'

test_expect_success 'diff skips same-OID blobs' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	echo b >server/b &&
	but -C server add a b &&
	but -C server cummit -m x &&
	echo another-a >server/a &&
	but -C server add a &&
	but -C server cummit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	but clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	echo a | but hash-object --stdin >hash-old-a &&
	echo another-a | but hash-object --stdin >hash-new-a &&
	echo b | but hash-object --stdin >hash-b &&

	# Ensure that only a and another-a are fetched.
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client diff HEAD^ HEAD &&
	grep "want $(cat hash-old-a)" trace &&
	grep "want $(cat hash-new-a)" trace &&
	! grep "want $(cat hash-b)" trace
'

test_expect_success 'when fetching missing objects, diff skips BUTLINKs' '
	test_when_finished "rm -rf sub server client trace" &&

	test_create_repo sub &&
	test_cummit -C sub first &&

	test_create_repo server &&
	echo a >server/a &&
	but -C server add a &&
	but -C server submodule add "file://$(pwd)/sub" &&
	but -C server cummit -m x &&

	test_cummit -C server/sub second &&
	echo another-a >server/a &&
	but -C server add a sub &&
	but -C server cummit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	but clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	echo a | but hash-object --stdin >hash-old-a &&
	echo another-a | but hash-object --stdin >hash-new-a &&

	# Ensure that a and another-a are fetched, and check (by successful
	# execution of the diff) that no invalid OIDs are sent.
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client diff HEAD^ HEAD &&
	grep "want $(cat hash-old-a)" trace &&
	grep "want $(cat hash-new-a)" trace
'

test_expect_success 'diff with rename detection batches blobs' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	printf "b\nb\nb\nb\nb\n" >server/b &&
	but -C server add a b &&
	but -C server cummit -m x &&
	rm server/b &&
	printf "b\nb\nb\nb\nbX\n" >server/c &&
	but -C server add c &&
	but -C server cummit -a -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	but clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	# Ensure that there is exactly 1 negotiation by checking that there is
	# only 1 "done" line sent. ("done" marks the end of negotiation.)
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client diff --raw -M HEAD^ HEAD >out &&
	grep ":100644 100644.*R[0-9][0-9][0-9].*b.*c" out &&
	grep "fetch> done" trace >done_lines &&
	test_line_count = 1 done_lines
'

test_expect_success 'diff does not fetch anything if inexact rename detection is not needed' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	printf "b\nb\nb\nb\nb\n" >server/b &&
	but -C server add a b &&
	but -C server cummit -m x &&
	mv server/b server/c &&
	but -C server add c &&
	but -C server cummit -a -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	but clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	# Ensure no fetches.
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client diff --raw -M HEAD^ HEAD &&
	! test_path_exists trace
'

test_expect_success 'diff --break-rewrites fetches only if necessary, and batches blobs if it does' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	printf "b\nb\nb\nb\nb\n" >server/b &&
	but -C server add a b &&
	but -C server cummit -m x &&
	printf "c\nc\nc\nc\nc\n" >server/b &&
	but -C server cummit -a -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	but clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	# Ensure no fetches.
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client diff --raw -M HEAD^ HEAD &&
	! test_path_exists trace &&

	# But with --break-rewrites, ensure that there is exactly 1 negotiation
	# by checking that there is only 1 "done" line sent. ("done" marks the
	# end of negotiation.)
	BUT_TRACE_PACKET="$(pwd)/trace" but -C client diff --break-rewrites --raw -M HEAD^ HEAD &&
	grep "fetch> done" trace >done_lines &&
	test_line_count = 1 done_lines
'

test_done
