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

hostdir="$PWD"
if test_have_prereq MINGW && test "/${PWD#/}" != "$PWD"
then
	case "$PWD" in
	[A-Za-z]:/*)
		hostdir="${PWD#?:}"
		;;
	*)
		skip_all="Unhandled PWD '$PWD'; skipping rest"
		test_done
		;;
	esac
fi

test_proto "ssh://" ssh "ssh://remote$hostdir/remote/repo.git"
test_proto "git+ssh://" ssh "git+ssh://remote$hostdir/remote/repo.git"

test_done
