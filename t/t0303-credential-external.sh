#!/bin/sh

test_description='external credential helper tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-credential.sh

if test -z "$GIT_TEST_CREDENTIAL_HELPER"; then
	skip_all="used to test external credential helpers"
	test_done
fi

test -z "$GIT_TEST_CREDENTIAL_HELPER_SETUP" ||
	eval "$GIT_TEST_CREDENTIAL_HELPER_SETUP"

# clean before the test in case there is cruft left
# over from a previous run that would impact results
helper_test_clean "$GIT_TEST_CREDENTIAL_HELPER"

helper_test "$GIT_TEST_CREDENTIAL_HELPER"

if test -z "$GIT_TEST_CREDENTIAL_HELPER_TIMEOUT"; then
	say "# skipping timeout tests (GIT_TEST_CREDENTIAL_HELPER_TIMEOUT not set)"
else
	helper_test_timeout "$GIT_TEST_CREDENTIAL_HELPER_TIMEOUT"
fi

# clean afterwards so that we are good citizens
# and don't leave cruft in the helper's storage, which
# might be long-term system storage
helper_test_clean "$GIT_TEST_CREDENTIAL_HELPER"

test_done
