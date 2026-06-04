#!/bin/sh

test_description='external credential helper tests

This is a tool for authors of external helper tools to sanity-check
their helpers. If you have written the "git-credential-foo" helper,
you check it with:

  make GIT_TEST_CREDENTIAL_HELPER=foo t0303-credential-external.sh

This assumes that your helper is capable of both storing and
retrieving credentials (some helpers may be read-only, and they will
fail these tests).

Please note that the individual tests do not verify all of the
preconditions themselves, but rather build on each other. A failing
test means that tests later in the sequence can return false "OK"
results.

If your helper supports time-based expiration with a configurable
timeout, you can test that feature with:

  make GIT_TEST_CREDENTIAL_HELPER=foo \
       GIT_TEST_CREDENTIAL_HELPER_TIMEOUT="foo --timeout=1" \
       t0303-credential-external.sh

If your helper requires additional setup before the tests are started,
you can set GIT_TEST_CREDENTIAL_HELPER_SETUP to a sequence of shell
commands.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-credential.sh

# If we're not given a specific external helper to run against,
# there isn't much to test. But we can still run through our
# battery of tests with a fake helper and check that the
# test themselves are self-consistent and clean up after
# themselves.
#
# We'll use the "store" helper, since we can easily inspect
# its state by looking at the on-disk file. But since it doesn't
# implement any caching or expiry logic, we'll cheat and override
# the "check" function to just report all results as OK.
if test -z "$GIT_TEST_CREDENTIAL_HELPER"; then
	GIT_TEST_CREDENTIAL_HELPER=store
	GIT_TEST_CREDENTIAL_HELPER_TIMEOUT=store
	check () {
		test "$1" = "approve" || return 0
		git -c credential.helper=store credential approve
	}
	check_cleanup=t
fi

test -z "$GIT_TEST_CREDENTIAL_HELPER_SETUP" ||
	eval "$GIT_TEST_CREDENTIAL_HELPER_SETUP"

# clean before the test in case there is cruft left
# over from a previous run that would impact results
helper_test_clean "$GIT_TEST_CREDENTIAL_HELPER"

helper_test "$GIT_TEST_CREDENTIAL_HELPER"
helper_test_password_expiry_utc "$GIT_TEST_CREDENTIAL_HELPER"
helper_test_oauth_refresh_token "$GIT_TEST_CREDENTIAL_HELPER"

if test -z "$GIT_TEST_CREDENTIAL_HELPER_TIMEOUT"; then
	say "# skipping timeout tests (GIT_TEST_CREDENTIAL_HELPER_TIMEOUT not set)"
else
	helper_test_timeout "$GIT_TEST_CREDENTIAL_HELPER_TIMEOUT"
fi

# clean afterwards so that we are good citizens
# and don't leave cruft in the helper's storage, which
# might be long-term system storage
helper_test_clean "$GIT_TEST_CREDENTIAL_HELPER"

if test "$check_cleanup" = "t"
then
	test_expect_success 'test cleanup removes everything' '
		test_must_be_empty "$HOME/.git-credentials"
	'
fi

test_done
