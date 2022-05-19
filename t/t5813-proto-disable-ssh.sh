#!/bin/sh

test_description='test disabling of but-over-ssh in clone/fetch'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"

setup_ssh_wrapper

test_expect_success 'setup repository to clone' '
	test_cummit one &&
	mkdir remote &&
	but init --bare remote/repo.but &&
	but push remote/repo.but HEAD
'

test_proto "host:path" ssh "remote:repo.but"
test_proto "ssh://" ssh "ssh://remote$PWD/remote/repo.but"
test_proto "but+ssh://" ssh "but+ssh://remote$PWD/remote/repo.but"

# Don't even bother setting up a "-remote" directory, as ssh would generally
# complain about the bogus option rather than completing our request. Our
# fake wrapper actually _can_ handle this case, but it's more robust to
# simply confirm from its output that it did not run at all.
test_expect_success 'hostnames starting with dash are rejected' '
	test_must_fail but clone ssh://-remote/repo.but dash-host 2>stderr &&
	! grep ^ssh: stderr
'

test_expect_success 'setup repo with dash' '
	but init --bare remote/-repo.but &&
	but push remote/-repo.but HEAD
'

test_expect_success 'repo names starting with dash are rejected' '
	test_must_fail but clone remote:-repo.but dash-path 2>stderr &&
	! grep ^ssh: stderr
'

test_expect_success 'full paths still work' '
	but clone "remote:$PWD/remote/-repo.but" dash-path
'

test_done
