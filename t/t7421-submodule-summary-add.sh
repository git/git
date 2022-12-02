#!/bin/sh
#
# Copyright (C) 2020 Shourya Shukla
#

test_description='Summary support for submodules, adding them using git submodule add

This test script tries to verify the sanity of summary subcommand of git submodule
while making sure to add submodules using `git submodule add` instead of
`git add` as done in t7401.
'

. ./test-lib.sh

test_expect_success 'setup' '
	git config --global protocol.file.allow always
'

test_expect_success 'summary test environment setup' '
	git init sm &&
	test_commit -C sm "add file" file file-content file-tag &&

	git submodule add ./sm my-subm &&
	test_tick &&
	git commit -m "add submodule"
'

test_expect_success 'submodule summary output for initialized submodule' '
	test_commit -C sm "add file2" file2 file2-content file2-tag &&
	git submodule update --remote &&
	test_tick &&
	git commit -m "update submodule" my-subm &&
	git submodule summary HEAD^ >actual &&
	rev1=$(git -C sm rev-parse --short HEAD^) &&
	rev2=$(git -C sm rev-parse --short HEAD) &&
	cat >expected <<-EOF &&
	* my-subm ${rev1}...${rev2} (1):
	  > add file2

	EOF
	test_cmp expected actual
'

test_expect_success 'submodule summary output for deinitialized submodule' '
	git submodule deinit my-subm &&
	git submodule summary HEAD^ >actual &&
	test_must_be_empty actual &&
	git submodule update --init my-subm &&
	git submodule summary HEAD^ >actual &&
	rev1=$(git -C sm rev-parse --short HEAD^) &&
	rev2=$(git -C sm rev-parse --short HEAD) &&
	cat >expected <<-EOF &&
	* my-subm ${rev1}...${rev2} (1):
	  > add file2

	EOF
	test_cmp expected actual
'

test_expect_success 'submodule summary output for submodules with changed paths' '
	git mv my-subm subm &&
	git commit -m "change submodule path" &&
	rev=$(git -C sm rev-parse --short HEAD^) &&
	git submodule summary HEAD^^ -- my-subm >actual 2>err &&
	test_must_be_empty err &&
	cat >expected <<-EOF &&
	* my-subm ${rev}...0000000:

	EOF
	test_cmp expected actual
'

test_done
