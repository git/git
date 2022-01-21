#!/bin/sh

test_description='Test cloning repos with submodules using remote-tracking branches'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

pwd=$(pwd)

test_expect_success 'setup' '
	git checkout -b main &&
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

# bare clone giving "srv.bare" for use as our server.
test_expect_success 'setup bare clone for server' '
	git clone --bare "file://$(pwd)/." srv.bare &&
	git -C srv.bare config --local uploadpack.allowfilter 1 &&
	git -C srv.bare config --local uploadpack.allowanysha1inwant 1
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
		git diff --exit-code remotes/origin/main
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
		git rev-parse --verify origin/main &&
		test_must_fail git rev-parse --verify origin/other
	)
'

# do basic partial clone from "srv.bare"
# confirm partial clone was registered in the local config for super and sub.
test_expect_success 'clone with --filter' '
	git clone --recurse-submodules --filter blob:none "file://$pwd/srv.bare" super_clone &&
	test_cmp_config -C super_clone 1 core.repositoryformatversion &&
	test_cmp_config -C super_clone blob:none remote.origin.partialclonefilter &&
	test_cmp_config -C super_clone/sub 1 core.repositoryformatversion &&
	test_cmp_config -C super_clone/sub blob:none remote.origin.partialclonefilter
'

test_done
