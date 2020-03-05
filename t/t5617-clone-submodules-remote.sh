#!/bin/sh

test_description='Test cloning repos with submodules using remote-tracking branches'

. ./test-lib.sh

pwd=$(pwd)

test_expect_success 'setup' '
	git checkout -b master &&
	test_commit commit1 &&
	mkdir sub &&
	(
		cd sub &&
		git init &&
		test_commit subcommit1 &&
		git tag sub_when_added_to_super &&
		git branch other
	) &&
	git submodule add "file://$pwd/sub" sub &&
	git commit -m "add submodule" &&
	(
		cd sub &&
		test_commit subcommit2
	)
'

test_expect_success 'clone with --no-remote-submodules' '
	test_when_finished "rm -rf super_clone" &&
	git clone --recurse-submodules --no-remote-submodules "file://$pwd/." super_clone &&
	(
		cd super_clone/sub &&
		git diff --exit-code sub_when_added_to_super
	)
'

test_expect_success 'clone with --remote-submodules' '
	test_when_finished "rm -rf super_clone" &&
	git clone --recurse-submodules --remote-submodules "file://$pwd/." super_clone &&
	(
		cd super_clone/sub &&
		git diff --exit-code remotes/origin/master
	)
'

test_expect_success 'check the default is --no-remote-submodules' '
	test_when_finished "rm -rf super_clone" &&
	git clone --recurse-submodules "file://$pwd/." super_clone &&
	(
		cd super_clone/sub &&
		git diff --exit-code sub_when_added_to_super
	)
'

test_expect_success 'clone with --single-branch' '
	test_when_finished "rm -rf super_clone" &&
	git clone --recurse-submodules --single-branch "file://$pwd/." super_clone &&
	(
		cd super_clone/sub &&
		git rev-parse --verify origin/master &&
		test_must_fail git rev-parse --verify origin/other
	)
'

test_done
