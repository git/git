#!/bin/sh

# Based on a test case submitted by Björn Steinbrink.

test_description='git blame on conflicted files'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup first case' '
	# Create the old file
	echo "Old line" > file1 &&
	git add file1 &&
	git commit --author "Old Line <ol@localhost>" -m file1.a &&

	# Branch
	git checkout -b foo &&

	# Do an ugly move and change
	git rm file1 &&
	echo "New line ..."  > file2 &&
	echo "... and more" >> file2 &&
	git add file2 &&
	git commit --author "U Gly <ug@localhost>" -m ugly &&

	# Back to main and change something
	git checkout main &&
	echo "

bla" >> file1 &&
	git commit --author "Old Line <ol@localhost>" -a -m file1.b &&

	# Back to foo and merge main
	git checkout foo &&
	if git merge main; then
		echo needed conflict here
		exit 1
	else
		echo merge failed - resolving automatically
	fi &&
	echo "New line ...
... and more

bla
Even more" > file2 &&
	git rm file1 &&
	git commit --author "M Result <mr@localhost>" -a -m merged &&

	# Back to main and change file1 again
	git checkout main &&
	sed s/bla/foo/ <file1 >X &&
	rm file1 &&
	mv X file1 &&
	git commit --author "No Bla <nb@localhost>" -a -m replace &&

	# Try to merge into foo again
	git checkout foo &&
	if git merge main; then
		echo needed conflict here
		exit 1
	else
		echo merge failed - test is setup
	fi
'

test_expect_success \
	'blame runs on unconflicted file while other file has conflicts' '
	git blame file2
'

test_expect_success 'blame does not crash with conflicted file in stages 1,3' '
	git blame file1
'

test_done
