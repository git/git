#!/bin/sh
#
# Copyright (c) 2007 Carl D. Worth
#

test_description='git ls-files test (--with-tree).

This test runs git ls-files --with-tree and in particular in
a scenario known to trigger a crash with some versions of git.
'
. ./test-lib.sh

test_expect_success setup '

	# The bug we are exercising requires a fair number of entries
	# in a sub-directory so that add_index_entry will trigger a
	# realloc.

	echo file >expected &&
	mkdir sub &&
	bad= &&
	for n in 0 1 2 3 4 5
	do
		for m in 0 1 2 3 4 5 6 7 8 9
		do
			num=00$n$m &&
			>sub/file-$num &&
			echo file-$num >>expected || {
				bad=t
				break
			}
		done && test -z "$bad" || {
			bad=t
			break
		}
	done && test -z "$bad" &&
	git add . &&
	git commit -m "add a bunch of files" &&

	# We remove them all so that we will have something to add
	# back with --with-tree and so that we will definitely be
	# under the realloc size to trigger the bug.
	rm -rf sub &&
	git commit -a -m "remove them all" &&

	# The bug also requires some entry before our directory so that
	# prune_path will modify the_index.cache

	mkdir a_directory_that_sorts_before_sub &&
	>a_directory_that_sorts_before_sub/file &&
	mkdir sub &&
	>sub/file &&
	git add .
'

# We have to run from a sub-directory to trigger prune_path
# Then we finally get to run our --with-tree test
cd sub

test_expect_success 'git -ls-files --with-tree should succeed from subdir' '

	git ls-files --with-tree=HEAD~1 >../output

'

cd ..
test_expect_success \
    'git -ls-files --with-tree should add entries from named tree.' \
    'diff -u expected output'

test_done
