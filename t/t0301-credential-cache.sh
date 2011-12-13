#!/bin/sh

test_description='credential-cache tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-credential.sh

test -z "$NO_UNIX_SOCKETS" || {
	skip_all='skipping credential-cache tests, unix sockets not available'
	test_done
}

# don't leave a stale daemon running
trap 'code=$?; git credential-cache exit; (exit $code); die' EXIT

helper_test cache
helper_test_timeout cache --timeout=1

# we can't rely on our "trap" above working after test_done,
# as test_done will delete the trash directory containing
# our socket, leaving us with no way to access the daemon.
git credential-cache exit

test_done
