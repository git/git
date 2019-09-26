#!/bin/sh

test_description='behavior of diff when reading objects in a partial clone'

. ./test-lib.sh

test_expect_success 'git show batches blobs' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	echo b >server/b &&
	git -C server add a b &&
	git -C server commit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	git clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	# Ensure that there is exactly 1 negotiation by checking that there is
	# only 1 "done" line sent. ("done" marks the end of negotiation.)
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client show HEAD &&
	grep "git> done" trace >done_lines &&
	test_line_count = 1 done_lines
'

test_expect_success 'diff batches blobs' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	echo b >server/b &&
	git -C server add a b &&
	git -C server commit -m x &&
	echo c >server/c &&
	echo d >server/d &&
	git -C server add c d &&
	git -C server commit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	git clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	# Ensure that there is exactly 1 negotiation by checking that there is
	# only 1 "done" line sent. ("done" marks the end of negotiation.)
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client diff HEAD^ HEAD &&
	grep "git> done" trace >done_lines &&
	test_line_count = 1 done_lines
'

test_expect_success 'diff skips same-OID blobs' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	echo b >server/b &&
	git -C server add a b &&
	git -C server commit -m x &&
	echo another-a >server/a &&
	git -C server add a &&
	git -C server commit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	git clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	echo a | git hash-object --stdin >hash-old-a &&
	echo another-a | git hash-object --stdin >hash-new-a &&
	echo b | git hash-object --stdin >hash-b &&

	# Ensure that only a and another-a are fetched.
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client diff HEAD^ HEAD &&
	grep "want $(cat hash-old-a)" trace &&
	grep "want $(cat hash-new-a)" trace &&
	! grep "want $(cat hash-b)" trace
'

test_expect_success 'when fetching missing objects, diff skips GITLINKs' '
	test_when_finished "rm -rf sub server client trace" &&

	test_create_repo sub &&
	test_commit -C sub first &&

	test_create_repo server &&
	echo a >server/a &&
	git -C server add a &&
	git -C server submodule add "file://$(pwd)/sub" &&
	git -C server commit -m x &&

	test_commit -C server/sub second &&
	echo another-a >server/a &&
	git -C server add a sub &&
	git -C server commit -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	git clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	echo a | git hash-object --stdin >hash-old-a &&
	echo another-a | git hash-object --stdin >hash-new-a &&

	# Ensure that a and another-a are fetched, and check (by successful
	# execution of the diff) that no invalid OIDs are sent.
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client diff HEAD^ HEAD &&
	grep "want $(cat hash-old-a)" trace &&
	grep "want $(cat hash-new-a)" trace
'

test_expect_success 'diff with rename detection batches blobs' '
	test_when_finished "rm -rf server client trace" &&

	test_create_repo server &&
	echo a >server/a &&
	printf "b\nb\nb\nb\nb\n" >server/b &&
	git -C server add a b &&
	git -C server commit -m x &&
	rm server/b &&
	printf "b\nb\nb\nb\nbX\n" >server/c &&
	git -C server add c &&
	git -C server commit -a -m x &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	git clone --bare --filter=blob:limit=0 "file://$(pwd)/server" client &&

	# Ensure that there is exactly 1 negotiation by checking that there is
	# only 1 "done" line sent. ("done" marks the end of negotiation.)
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client diff -M HEAD^ HEAD >out &&
	grep "similarity index" out &&
	grep "git> done" trace >done_lines &&
	test_line_count = 1 done_lines
'

test_done
