#!/bin/sh

test_description='verify safe.directory checks while running as root'

. ./test-lib.sh

if [ "$GIT_TEST_ALLOW_SUDO" != "YES" ]
then
	skip_all="You must set env var GIT_TEST_ALLOW_SUDO=YES in order to run this test"
	test_done
fi

test_lazy_prereq SUDO '
	sudo -n id -u >u &&
	id -u root >r &&
	test_cmp u r &&
	command -v git >u &&
	sudo command -v git >r &&
	test_cmp u r
'

test_expect_success SUDO 'setup' '
	sudo rm -rf root &&
	mkdir -p root/r &&
	(
		cd root/r &&
		git init
	)
'

test_expect_failure SUDO 'sudo git status as original owner' '
	(
		cd root/r &&
		git status &&
		sudo git status
	)
'

# this MUST be always the last test
test_expect_success SUDO 'cleanup' '
	sudo rm -rf root
'

test_done
