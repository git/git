#!/bin/sh
#
# Test Git in parallel
#

. ${0%/*}/lib.sh

group "Run tests" make --quiet -C t T="$(cd t &&
	./helper/test-tool path-utils slice-tests "$1" "$2" t[0-9]*.sh |
	tr '\n' ' ')" ||
handle_failed_tests

# We only have one unit test at the moment, so run it in the first slice
if [ "$1" == "0" ] ; then
	group "Run unit tests" make --quiet -C t unit-tests-test-tool
fi

# Run the git subtree tests only if main tests succeeded
test 0 != "$1" || make -C contrib/subtree test

check_unignored_build_artifacts
