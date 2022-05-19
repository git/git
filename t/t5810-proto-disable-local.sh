#!/bin/sh

test_description='test disabling of local paths in clone/fetch'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"

test_expect_success 'setup repository to clone' '
	test_cummit one
'

test_proto "file://" file "file://$PWD"
test_proto "path" file .

test_expect_success 'setup repo with dash' '
	but init --bare repo.but &&
	but push repo.but HEAD &&
	mv repo.but "$PWD/-repo.but"
'

# This will fail even without our rejection because upload-pack will
# complain about the bogus option. So let's make sure that BUT_TRACE
# doesn't show us even running upload-pack.
#
# We must also be sure to use "fetch" and not "clone" here, as the latter
# actually canonicalizes our input into an absolute path (which is fine
# to allow).
test_expect_success 'repo names starting with dash are rejected' '
	rm -f trace.out &&
	test_must_fail env BUT_TRACE="$PWD/trace.out" but fetch -- -repo.but &&
	! grep upload-pack trace.out
'

test_expect_success 'full paths still work' '
	but fetch "$PWD/-repo.but"
'

test_done
