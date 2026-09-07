#!/bin/sh

test_description='apply-one-time-script CGI helper is safe under concurrent requests'

. ./test-lib.sh

HELPER="$TEST_DIRECTORY/lib-httpd/apply-one-time-script.sh"

test_expect_success PIPE 'helper only serves one rewritten response for concurrent requests' '
	mkdir workdir fakebin &&
	ENTERED="$PWD/entered" &&
	GATE="$PWD/gate" &&
	export ENTERED GATE &&
	mkfifo "$ENTERED" "$GATE" &&

	# A stub git-http-backend that returns a response based on
	# $ROLE. For $ROLE = modify, return the response string
	# "packfile", which ends up being modified by the example
	# one-time-script below.
	#
	# Otherwise, run the branch returning a response that
	# should be passed through, and block until released
	# by "read -r $GATE".
	write_script fakebin/git-http-backend <<-\EOF &&
	printf "Status: 200 OK\r\n"
	printf "Content-Type: application/x-git-result\r\n"
	printf "\r\n"
	if test "$ROLE" = modify
	then
		printf "packfile\n"
	else
		echo entered >"$ENTERED"
		read -r released <"$GATE"
		printf "refs\n"
	fi
	EOF

	# An example one-time-script for apply-one-time-script
	# to execute. Checks for "packfile" in the response
	# that will be returned, and replaces it with a
	# modified response. Passes through responses without
	# "packfile" in them.
	write_script workdir/one-time-script <<-\EOF &&
	if grep packfile "$1" >/dev/null
	then
		sed "/packfile/q" "$1" &&
		printf "REPLACED\n"
	else
		cat "$1"
	fi
	EOF

	GIT_EXEC_PATH="$PWD/fakebin" &&
	export GIT_EXEC_PATH &&

	# Ensure $GATE has a reader so the test does not block indefinitely if
	# the helper is buggy and "echo released >&9" below does not unblock
	# the unmodified response gate.
	exec 9<>"$GATE" &&

	# Launch the passthrough request in the background. Record its pid
	# so it can be killed when the test finishes if, for some reason, the
	# request stays blocked and would stall a test runner.
	{ (
		cd workdir &&
		ROLE=passthrough sh "$HELPER" >../passthrough.out 2>../passthrough.err
	) & } &&
	passthrough_pid=$! &&
	test_when_finished "kill $passthrough_pid 2>/dev/null || :" &&

	# Wait until the passthrough request is "in-flight" and paused
	# mid-response.
	read -r entered <"$ENTERED" &&

	# Launch the request for a modified response while the passthrough
	# request is concurrently "in-flight" and paused.
	(
		cd workdir &&
		ROLE=modify sh "$HELPER" >../modify.out 2>../modify.err
	) &&

	# Unblock the passthrough request, allowing git-http-backend to
	# complete its response.
	echo released >&9 &&
	{ wait "$passthrough_pid" || :; } &&

	test_must_be_empty passthrough.err &&
	test_must_be_empty modify.err &&
	test_grep "Status: 200 OK" passthrough.out &&
	test_grep "Status: 200 OK" modify.out &&
	test_grep REPLACED modify.out &&
	test_grep ! REPLACED passthrough.out &&
	test_grep refs passthrough.out
'

test_done
