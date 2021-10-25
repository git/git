#!/bin/sh
#
# Copyright (C) 2018  Antonio Ospite <ao2@ao2.it>
#

test_description='Test reading/writing .gitmodules when not in the working tree

This test verifies that, when .gitmodules is in the current branch but is not
in the working tree reading from it still works but writing to it does not.

The test setup uses a sparse checkout, however the same scenario can be set up
also by committing .gitmodules and then just removing it from the filesystem.
'

GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB

. ./test-lib.sh

test_expect_success 'sparse checkout setup which hides .gitmodules' '
	git init upstream &&
	git init submodule &&
	(cd submodule &&
		echo file >file &&
		git add file &&
		test_tick &&
		git commit -m "Add file"
	) &&
	(cd upstream &&
		git submodule add ../submodule &&
		test_tick &&
		git commit -m "Add submodule"
	) &&
	git clone upstream super &&
	(cd super &&
		cat >.git/info/sparse-checkout <<-\EOF &&
		/*
		!/.gitmodules
		EOF
		git config core.sparsecheckout true &&
		git read-tree -m -u HEAD &&
		test_path_is_missing .gitmodules
	)
'

test_expect_success 'reading gitmodules config file when it is not checked out' '
	echo "../submodule" >expect &&
	git -C super submodule--helper config submodule.submodule.url >actual &&
	test_cmp expect actual
'

test_expect_success 'not writing gitmodules config file when it is not checked out' '
	test_must_fail git -C super submodule--helper config submodule.submodule.url newurl &&
	test_path_is_missing super/.gitmodules
'

test_expect_success 'initialising submodule when the gitmodules config is not checked out' '
	test_must_fail git -C super config submodule.submodule.url &&
	git -C super submodule init &&
	git -C super config submodule.submodule.url >actual &&
	echo "$(pwd)/submodule" >expect &&
	test_cmp expect actual
'

test_expect_success 'updating submodule when the gitmodules config is not checked out' '
	test_path_is_missing super/submodule/file &&
	git -C super submodule update &&
	test_cmp submodule/file super/submodule/file
'

ORIG_SUBMODULE=$(git -C submodule rev-parse HEAD)
ORIG_UPSTREAM=$(git -C upstream rev-parse HEAD)
ORIG_SUPER=$(git -C super rev-parse HEAD)

test_expect_success 're-updating submodule when the gitmodules config is not checked out' '
	test_when_finished "git -C submodule reset --hard $ORIG_SUBMODULE;
			    git -C upstream reset --hard $ORIG_UPSTREAM;
			    git -C super reset --hard $ORIG_SUPER;
			    git -C upstream submodule update --remote;
			    git -C super pull;
			    git -C super submodule update --remote" &&
	(cd submodule &&
		echo file2 >file2 &&
		git add file2 &&
		test_tick &&
		git commit -m "Add file2 to submodule"
	) &&
	(cd upstream &&
		git submodule update --remote &&
		git add submodule &&
		test_tick &&
		git commit -m "Update submodule"
	) &&
	git -C super pull &&
	# The --for-status options reads the gitmodules config
	git -C super submodule summary --for-status >actual &&
	rev1=$(git -C submodule rev-parse --short HEAD) &&
	rev2=$(git -C submodule rev-parse --short HEAD^) &&
	cat >expect <<-EOF &&
	* submodule ${rev1}...${rev2} (1):
	  < Add file2 to submodule

	EOF
	test_cmp expect actual &&
	# Test that the update actually succeeds
	test_path_is_missing super/submodule/file2 &&
	git -C super submodule update &&
	test_cmp submodule/file2 super/submodule/file2 &&
	git -C super status --short >output &&
	test_must_be_empty output
'

test_expect_success 'not adding submodules when the gitmodules config is not checked out' '
	git clone submodule new_submodule &&
	test_must_fail git -C super submodule add ../new_submodule &&
	test_path_is_missing .gitmodules
'

# This test checks that the previous "git submodule add" did not leave the
# repository in a spurious state when it failed.
test_expect_success 'init submodule still works even after the previous add failed' '
	git -C super submodule init
'

test_done
