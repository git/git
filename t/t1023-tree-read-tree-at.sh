#!/bin/sh

# tests for tree.c (not read-tree.c)
test_description='Test read_tree / read_tree_at'
. ./test-lib.sh

test_expect_success 'read_tree basic' '
	rm -rf walk_tree_basic &&
	git init walk_tree_basic &&
	(
		cd walk_tree_basic &&
		set -x &&

		mkdir -p dir1/dirA &&
		mkdir -p dir1/dirB &&
		mkdir -p dir2 &&
		echo "file1" > file1.txt &&
		echo "file2" > file2.txt &&
		# uncommitted
		echo "file3" > file3.txt &&

		echo "file1A1" > dir1/dirA/file1.txt &&
		git add file1.txt file2.txt dir1/dirA/file1.txt &&
		git commit -m "initial commit" &&

		test-tool tree-read-tree-at . > walk1.txt &&
		grep " file1.txt" walk1.txt &&
		! grep " file3.txt" walk1.txt &&
		! grep " dir1/dirB" walk1.txt &&
		grep " dir1/dirA/file1.txt" walk1.txt
	)
'

test_expect_success 'read_tree submodules' '
	rm -rf walk_tree_submodules &&
	git init submodule1 &&
	(
		cd submodule1 &&
		mkdir -p dir1/dirA &&
		echo "dir2/sub1/file1.txt" > file1.txt &&
		echo "dir2/sub1/file1A1.txt" > dir1/dirA/file1.txt &&
		git add file1.txt dir1/dirA/file1.txt &&
		git commit -m "initial commit"
	) &&
	git init walk_tree_submodules &&
	(
		cd walk_tree_submodules &&

		mkdir -p dir2 &&
		echo "file1" > file1.txt &&
		echo "dir2/file2" > dir2/file2.txt &&
		git add file1.txt dir2/file2.txt &&
		git commit -m "initial commit" &&

		git submodule add ../submodule1 dir2/sub1 &&
		git commit -m "add submodule1" &&

		test-tool tree-read-tree-at . > walk2.txt &&
		grep " file1.txt" walk2.txt &&
		grep " dir2/sub1/file1.txt" walk2.txt &&
		grep " dir2/sub1/dir1/dirA/file1.txt" walk2.txt
	)
'

test_done
