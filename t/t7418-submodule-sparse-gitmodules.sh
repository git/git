#!/bin/sh
#
# Copyright (C) 2018  Antonio Ospite <ao2@ao2.it>
#

test_description='Test reading/writing .butmodules when not in the working tree

This test verifies that, when .butmodules is in the current branch but is not
in the working tree reading from it still works but writing to it does not.

The test setup uses a sparse checkout, however the same scenario can be set up
also by cummitting .butmodules and then just removing it from the filesystem.
'

GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB

. ./test-lib.sh

test_expect_success 'sparse checkout setup which hides .butmodules' '
	but init upstream &&
	but init submodule &&
	(cd submodule &&
		echo file >file &&
		but add file &&
		test_tick &&
		but cummit -m "Add file"
	) &&
	(cd upstream &&
		but submodule add ../submodule &&
		test_tick &&
		but cummit -m "Add submodule"
	) &&
	but clone upstream super &&
	(cd super &&
		cat >.but/info/sparse-checkout <<-\EOF &&
		/*
		!/.butmodules
		EOF
		but config core.sparsecheckout true &&
		but read-tree -m -u HEAD &&
		test_path_is_missing .butmodules
	)
'

test_expect_success 'reading butmodules config file when it is not checked out' '
	echo "../submodule" >expect &&
	but -C super submodule--helper config submodule.submodule.url >actual &&
	test_cmp expect actual
'

test_expect_success 'not writing butmodules config file when it is not checked out' '
	test_must_fail but -C super submodule--helper config submodule.submodule.url newurl &&
	test_path_is_missing super/.butmodules
'

test_expect_success 'initialising submodule when the butmodules config is not checked out' '
	test_must_fail but -C super config submodule.submodule.url &&
	but -C super submodule init &&
	but -C super config submodule.submodule.url >actual &&
	echo "$(pwd)/submodule" >expect &&
	test_cmp expect actual
'

test_expect_success 'updating submodule when the butmodules config is not checked out' '
	test_path_is_missing super/submodule/file &&
	but -C super submodule update &&
	test_cmp submodule/file super/submodule/file
'

ORIG_SUBMODULE=$(but -C submodule rev-parse HEAD)
ORIG_UPSTREAM=$(but -C upstream rev-parse HEAD)
ORIG_SUPER=$(but -C super rev-parse HEAD)

test_expect_success 're-updating submodule when the butmodules config is not checked out' '
	test_when_finished "but -C submodule reset --hard $ORIG_SUBMODULE;
			    but -C upstream reset --hard $ORIG_UPSTREAM;
			    but -C super reset --hard $ORIG_SUPER;
			    but -C upstream submodule update --remote;
			    but -C super pull;
			    but -C super submodule update --remote" &&
	(cd submodule &&
		echo file2 >file2 &&
		but add file2 &&
		test_tick &&
		but cummit -m "Add file2 to submodule"
	) &&
	(cd upstream &&
		but submodule update --remote &&
		but add submodule &&
		test_tick &&
		but cummit -m "Update submodule"
	) &&
	but -C super pull &&
	# The --for-status options reads the butmodules config
	but -C super submodule summary --for-status >actual &&
	rev1=$(but -C submodule rev-parse --short HEAD) &&
	rev2=$(but -C submodule rev-parse --short HEAD^) &&
	cat >expect <<-EOF &&
	* submodule ${rev1}...${rev2} (1):
	  < Add file2 to submodule

	EOF
	test_cmp expect actual &&
	# Test that the update actually succeeds
	test_path_is_missing super/submodule/file2 &&
	but -C super submodule update &&
	test_cmp submodule/file2 super/submodule/file2 &&
	but -C super status --short >output &&
	test_must_be_empty output
'

test_expect_success 'not adding submodules when the butmodules config is not checked out' '
	but clone submodule new_submodule &&
	test_must_fail but -C super submodule add ../new_submodule &&
	test_path_is_missing .butmodules
'

# This test checks that the previous "but submodule add" did not leave the
# repository in a spurious state when it failed.
test_expect_success 'init submodule still works even after the previous add failed' '
	but -C super submodule init
'

test_done
