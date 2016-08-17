#!/bin/sh
#
# Copyright (c) 2009, Red Hat Inc, Author: Michael S. Tsirkin (mst@redhat.com)
#

test_description='test clone --reference'
. ./test-lib.sh

base_dir=$(pwd)

test_alternate_is_used () {
	alternates_file="$1" &&
	working_dir="$2" &&
	test_line_count = 1 "$alternates_file" &&
	echo "0 objects, 0 kilobytes" >expect &&
	git -C "$working_dir" count-objects >actual &&
	test_cmp expect actual
}

test_expect_success 'preparing first repository' '
	test_create_repo A &&
	(
		cd A &&
		echo first >file1 &&
		git add file1 &&
		git commit -m A-initial
	)
'

test_expect_success 'preparing second repository' '
	git clone A B &&
	(
		cd B &&
		echo second >file2 &&
		git add file2 &&
		git commit -m B-addition &&
		git repack -a -d &&
		git prune
	)
'

test_expect_success 'preparing superproject' '
	test_create_repo super &&
	(
		cd super &&
		echo file >file &&
		git add file &&
		git commit -m B-super-initial
	)
'

test_expect_success 'submodule add --reference uses alternates' '
	(
		cd super &&
		git submodule add --reference ../B "file://$base_dir/A" sub &&
		git commit -m B-super-added &&
		git repack -ad
	) &&
	test_alternate_is_used super/.git/modules/sub/objects/info/alternates super/sub
'

test_expect_success 'that reference gets used with add' '
	(
		cd super/sub &&
		echo "0 objects, 0 kilobytes" >expected &&
		git count-objects >current &&
		diff expected current
	)
'

# The tests up to this point, and repositories created by them
# (A, B, super and super/sub), are about setting up the stage
# for subsequent tests and meant to be kept throughout the
# remainder of the test.
# Tests from here on, if they create their own test repository,
# are expected to clean after themselves.

test_expect_success 'updating superproject keeps alternates' '
	test_when_finished "rm -rf super-clone" &&
	git clone super super-clone &&
	git -C super-clone submodule update --init --reference ../B &&
	test_alternate_is_used super-clone/.git/modules/sub/objects/info/alternates super-clone/sub
'

test_expect_success 'submodules use alternates when cloning a superproject' '
	test_when_finished "rm -rf super-clone" &&
	git clone --reference super --recursive super super-clone &&
	(
		cd super-clone &&
		# test superproject has alternates setup correctly
		test_alternate_is_used .git/objects/info/alternates . &&
		# test submodule has correct setup
		test_alternate_is_used .git/modules/sub/objects/info/alternates sub
	)
'

test_expect_success 'missing submodule alternate fails clone and submodule update' '
	test_when_finished "rm -rf super-clone" &&
	git clone super super2 &&
	test_must_fail git clone --recursive --reference super2 super2 super-clone &&
	(
		cd super-clone &&
		# test superproject has alternates setup correctly
		test_alternate_is_used .git/objects/info/alternates . &&
		# update of the submodule succeeds
		test_must_fail git submodule update --init &&
		# and we have no alternates:
		test_must_fail test_alternate_is_used .git/modules/sub/objects/info/alternates sub &&
		test_must_fail test_path_is_file sub/file1
	)
'

test_expect_success 'ignoring missing submodule alternates passes clone and submodule update' '
	test_when_finished "rm -rf super-clone" &&
	git clone --reference-if-able super2 --recursive super2 super-clone &&
	(
		cd super-clone &&
		# test superproject has alternates setup correctly
		test_alternate_is_used .git/objects/info/alternates . &&
		# update of the submodule succeeds
		git submodule update --init &&
		# and we have no alternates:
		test_must_fail test_alternate_is_used .git/modules/sub/objects/info/alternates sub &&
		test_path_is_file sub/file1
	)
'

test_done
