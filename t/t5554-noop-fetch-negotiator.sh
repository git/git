#!/bin/sh

test_description='test noop fetch negotiator'

. ./test-lib.sh

test_expect_success 'noop negotiator does not emit any "have"' '
	rm -f trace &&

	test_create_repo server &&
	test_commit -C server to_fetch &&

	test_create_repo client &&
	test_commit -C client we_have &&

	test_config -C client fetch.negotiationalgorithm noop &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client fetch "$(pwd)/server" &&

	! grep "fetch> have" trace &&
	grep "fetch> done" trace
'

test_done
