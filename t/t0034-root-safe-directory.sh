#!/bin/sh

test_description='verify safe.directory checks while running as root'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-sudo.sh

if [ "$GIT_TEST_ALLOW_SUDO" != "YES" ]
then
	skip_all="You must set env var GIT_TEST_ALLOW_SUDO=YES in order to run this test"
	test_done
fi

if ! test_have_prereq NOT_ROOT
then
	skip_all="These tests do not support running as root"
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

if ! test_have_prereq SUDO
then
	skip_all="Your sudo/system configuration is either too strict or unsupported"
	test_done
fi

test_expect_success SUDO 'setup' '
	sudo rm -rf root &&
	mkdir -p root/r &&
	(
		cd root/r &&
		git init
	)
'

test_expect_success SUDO 'sudo git status as original owner' '
	(
		cd root/r &&
		git status &&
		sudo git status
	)
'

test_expect_success SUDO 'setup root owned repository' '
	sudo mkdir -p root/p &&
	sudo git init root/p
'

test_expect_success 'cannot access if owned by root' '
	(
		cd root/p &&
		test_must_fail git status
	)
'

test_expect_success 'can access if addressed explicitly' '
	(
		cd root/p &&
		GIT_DIR=.git GIT_WORK_TREE=. git status
	)
'

test_expect_success SUDO 'can access with sudo if root' '
	(
		cd root/p &&
		sudo git status
	)
'

test_expect_success SUDO 'can access with sudo if root by removing SUDO_UID' '
	(
		cd root/p &&
		run_with_sudo <<-END
			unset SUDO_UID &&
			git status
		END
	)
'

# this MUST be always the last test
test_expect_success SUDO 'cleanup' '
	sudo rm -rf root
'

test_done
