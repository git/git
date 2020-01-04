#!/bin/sh

test_description='test disabling of git-over-ssh in clone/fetch'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-proto-disable.sh"

setup_ssh_wrapper

test_expect_success 'setup repository to clone' '
	test_commit one &&
	mkdir remote &&
	git init --bare remote/repo.git &&
	git push remote/repo.git HEAD
'

test_proto "host:path" ssh "remote:repo.git"
test_proto "ssh://" ssh "ssh://remote$PWD/remote/repo.git"
test_proto "git+ssh://" ssh "git+ssh://remote$PWD/remote/repo.git"

# Don't even bother setting up a "-remote" directory, as ssh would generally
# complain about the bogus option rather than completing our request. Our
# fake wrapper actually _can_ handle this case, but it's more robust to
# simply confirm from its output that it did not run at all.
test_expect_success 'hostnames starting with dash are rejected' '
	test_must_fail git clone ssh://-remote/repo.git dash-host 2>stderr &&
	! grep ^ssh: stderr
'

test_expect_success 'setup repo with dash' '
	git init --bare remote/-repo.git &&
	git push remote/-repo.git HEAD
'

test_expect_success 'repo names starting with dash are rejected' '
	test_must_fail git clone remote:-repo.git dash-path 2>stderr &&
	! grep ^ssh: stderr
'

test_expect_success 'full paths still work' '
	git clone "remote:$PWD/remote/-repo.git" dash-path
'

test_done
