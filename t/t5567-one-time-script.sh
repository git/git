#!/bin/sh

test_description='apply-one-time-script CGI helper is safe under concurrent requests'

. ./test-lib.sh

HELPER="$TEST_DIRECTORY/lib-httpd/apply-one-time-script.sh"

test_expect_success PIPE 'concurrent requests: one rewritten, one passed through, neither empty' '
	mkdir workdir fakebin &&
	ENTERED="$PWD/entered" &&
	GATE="$PWD/gate" &&
	export ENTERED GATE &&
	mkfifo "$ENTERED" "$GATE" &&

	# Stand in for git-http-backend. The modify role returns a response
	# containing "packfile", which the one-time script rewrites. The
	# passthrough role returns a response that is left untouched, but first
	# announces that it has entered the helper and then blocks, so that it
	# is still in flight when the modify role claims and removes the marker.
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

	# The transform that replace_packfile would install as one-time-script:
	# rewrite responses that contain "packfile", leave the rest alone.
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

	# Hold GATE open read-write on fd 9 for the duration, so releasing the
	# passthrough request below cannot block even if that request has
	# already exited (it keeps a reader on the FIFO).
	exec 9<>"$GATE" &&

	# Launch the passthrough request in the background. It enters the
	# helper, signals us through ENTERED, then blocks on GATE inside the
	# fake backend. The braces keep the && chain intact while backgrounding
	# only the subshell, so "wait" can reap it by pid; kill it on any exit
	# so a stray blocked child cannot hold the test output open and stall a
	# reader such as prove.
	{ (
		cd workdir &&
		ROLE=passthrough sh "$HELPER" >../passthrough.out 2>../passthrough.err
	) & } &&
	passthrough_pid=$! &&
	test_when_finished "kill $passthrough_pid 2>/dev/null || :" &&

	# Wait until the passthrough request is past the marker check.
	read -r entered <"$ENTERED" &&

	# Run the modifying request to completion while the passthrough request
	# is still blocked.
	(
		cd workdir &&
		ROLE=modify sh "$HELPER" >../modify.out 2>../modify.err
	) &&

	# Release the passthrough request and let it finish. Ignore the helper
	# exit status here so a broken helper is diagnosed by the assertions
	# below rather than aborting the test.
	echo released >&9 &&
	{ wait "$passthrough_pid" || :; } &&

	# Neither request may error out or produce an empty (HTTP 500) body,
	# and each must have played its role: the modify request rewrote its
	# response and the passthrough request came through untouched.
	test_must_be_empty passthrough.err &&
	test_must_be_empty modify.err &&
	test_grep "Status: 200 OK" passthrough.out &&
	test_grep "Status: 200 OK" modify.out &&
	test_grep REPLACED modify.out &&
	test_grep ! REPLACED passthrough.out &&
	test_grep refs passthrough.out
'

test_done
