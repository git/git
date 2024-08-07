#!/bin/sh
#
# Copyright (c) 2007 Carl D. Worth
#

test_description='git ls-files test (--with-tree).

This test runs git ls-files --with-tree and in particular in
a scenario known to trigger a crash with some versions of git.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '

	# The bug we are exercising requires a fair number of entries
	# in a sub-directory so that add_index_entry will trigger a
	# realloc.

	echo file >expected &&
	mkdir sub &&
	for n in 0 1 2 3 4 5
	do
		for m in 0 1 2 3 4 5 6 7 8 9
		do
			num=00$n$m &&
			>sub/file-$num &&
			echo file-$num >>expected ||
			return 1
		done
	done &&
	git add . &&
	git commit -m "add a bunch of files" &&

	# We remove them all so that we will have something to add
	# back with --with-tree and so that we will definitely be
	# under the realloc size to trigger the bug.
	rm -rf sub &&
	git commit -a -m "remove them all" &&

	# The bug also requires some entry before our directory so that
	# prune_index will modify the_repository->index.cache

	mkdir a_directory_that_sorts_before_sub &&
	>a_directory_that_sorts_before_sub/file &&
	mkdir sub &&
	>sub/file &&
	git add .
'

test_expect_success 'usage' '
	test_expect_code 128 git ls-files --with-tree=HEAD -u &&
	test_expect_code 128 git ls-files --with-tree=HEAD -s &&
	test_expect_code 128 git ls-files --recurse-submodules --with-tree=HEAD
'

test_expect_success 'git ls-files --with-tree should succeed from subdir' '
	# We have to run from a sub-directory to trigger prune_index
	# Then we finally get to run our --with-tree test
	(
		cd sub &&
		git ls-files --with-tree=HEAD~1 >../output
	)
'

test_expect_success 'git ls-files --with-tree should add entries from named tree.' '
	test_cmp expected output
'

test_expect_success 'no duplicates in --with-tree output' '
	git ls-files --with-tree=HEAD >actual &&
	sort -u actual >expected &&
	test_cmp expected actual
'

test_expect_success 'setup: output in a conflict' '
	test_create_repo conflict &&
	test_commit -C conflict BASE file &&
	test_commit -C conflict A file foo &&
	git -C conflict reset --hard BASE &&
	test_commit -C conflict B file bar
'

test_expect_success 'output in a conflict' '
	test_must_fail git -C conflict merge A B &&
	cat >expected <<-\EOF &&
	file
	file
	file
	file
	EOF
	git -C conflict ls-files --with-tree=HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'output with removed .git/index' '
	cat >expected <<-\EOF &&
	file
	EOF
	rm conflict/.git/index &&
	git -C conflict ls-files --with-tree=HEAD >actual &&
	test_cmp expected actual
'

test_done
