#!/bin/sh

test_description='test disabling of local paths in clone/fetch'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"

test_expect_success 'setup repository to clone' '
	test_commit one
'

test_proto "file://" file "file://$PWD"
test_proto "path" file .

test_expect_success 'setup repo with dash' '
	git init --bare repo.git &&
	git push repo.git HEAD &&
	mv repo.git "$PWD/-repo.git"
'

# This will fail even without our rejection because upload-pack will
# complain about the bogus option. So let's make sure that GIT_TRACE
# doesn't show us even running upload-pack.
#
# We must also be sure to use "fetch" and not "clone" here, as the latter
# actually canonicalizes our input into an absolute path (which is fine
# to allow).
test_expect_success 'repo names starting with dash are rejected' '
	rm -f trace.out &&
	test_must_fail env GIT_TRACE="$PWD/trace.out" git fetch -- -repo.git &&
	! grep upload-pack trace.out
'

test_expect_success 'full paths still work' '
	git fetch "$PWD/-repo.git"
'

test_done
