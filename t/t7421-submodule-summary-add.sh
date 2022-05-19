#!/bin/sh
#
# Copyright (C) 2020 Shourya Shukla
#

test_description='Summary support for submodules, adding them using but submodule add

This test script tries to verify the sanity of summary subcommand of but submodule
while making sure to add submodules using `but submodule add` instead of
`but add` as done in t7401.
'

. ./test-lib.sh

test_expect_success 'summary test environment setup' '
	but init sm &&
	test_cummit -C sm "add file" file file-content file-tag &&

	but submodule add ./sm my-subm &&
	test_tick &&
	but cummit -m "add submodule"
'

test_expect_success 'submodule summary output for initialized submodule' '
	test_cummit -C sm "add file2" file2 file2-content file2-tag &&
	but submodule update --remote &&
	test_tick &&
	but cummit -m "update submodule" my-subm &&
	but submodule summary HEAD^ >actual &&
	rev1=$(but -C sm rev-parse --short HEAD^) &&
	rev2=$(but -C sm rev-parse --short HEAD) &&
	cat >expected <<-EOF &&
	* my-subm ${rev1}...${rev2} (1):
	  > add file2

	EOF
	test_cmp expected actual
'

test_expect_success 'submodule summary output for deinitialized submodule' '
	but submodule deinit my-subm &&
	but submodule summary HEAD^ >actual &&
	test_must_be_empty actual &&
	but submodule update --init my-subm &&
	but submodule summary HEAD^ >actual &&
	rev1=$(but -C sm rev-parse --short HEAD^) &&
	rev2=$(but -C sm rev-parse --short HEAD) &&
	cat >expected <<-EOF &&
	* my-subm ${rev1}...${rev2} (1):
	  > add file2

	EOF
	test_cmp expected actual
'

test_expect_success 'submodule summary output for submodules with changed paths' '
	but mv my-subm subm &&
	but cummit -m "change submodule path" &&
	rev=$(but -C sm rev-parse --short HEAD^) &&
	but submodule summary HEAD^^ -- my-subm >actual 2>err &&
	test_must_be_empty err &&
	cat >expected <<-EOF &&
	* my-subm ${rev}...0000000:

	EOF
	test_cmp expected actual
'

test_done
