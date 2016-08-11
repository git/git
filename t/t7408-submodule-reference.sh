#!/bin/sh
#
# Copyright (c) 2009, Red Hat Inc, Author: Michael S. Tsirkin (mst@redhat.com)
#

test_description='test clone --reference'
. ./test-lib.sh

base_dir=$(pwd)

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

test_expect_success 'submodule add --reference' '
	(
		cd super &&
		git submodule add --reference ../B "file://$base_dir/A" sub &&
		git commit -m B-super-added
	)
'

test_expect_success 'after add: existence of info/alternates' '
	test_line_count = 1 super/.git/modules/sub/objects/info/alternates
'

test_expect_success 'that reference gets used with add' '
	(
		cd super/sub &&
		echo "0 objects, 0 kilobytes" >expected &&
		git count-objects >current &&
		diff expected current
	)
'

test_expect_success 'cloning superproject' '
	git clone super super-clone
'

test_expect_success 'update with reference' '
	cd super-clone && git submodule update --init --reference ../B
'

test_expect_success 'after update: existence of info/alternates' '
	test_line_count = 1 super-clone/.git/modules/sub/objects/info/alternates
'

test_expect_success 'that reference gets used with update' '
	cd super-clone/sub &&
	echo "0 objects, 0 kilobytes" >expected &&
	git count-objects >current &&
	diff expected current
'

test_done
