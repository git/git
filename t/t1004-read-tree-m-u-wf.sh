#!/bin/sh

test_description='read-tree -m -u checks working tree files'

. ./test-lib.sh

# two-tree test

test_expect_success 'two-way setup' '

	mkdir subdir &&
	echo >file1 file one &&
	echo >file2 file two &&
	echo >subdir/file1 file one in subdirectory &&
	echo >subdir/file2 file two in subdirectory &&
	git update-index --add file1 file2 subdir/file1 subdir/file2 &&
	git commit -m initial &&

	git branch side &&
	git tag -f branch-point &&

	echo file2 is not tracked on the master anymore &&
	rm -f file2 subdir/file2 &&
	git update-index --remove file2 subdir/file2 &&
	git commit -a -m "master removes file2 and subdir/file2"
'

test_expect_success 'two-way not clobbering' '

	echo >file2 master creates untracked file2 &&
	echo >subdir/file2 master creates untracked subdir/file2 &&
	if err=`git read-tree -m -u master side 2>&1`
	then
		echo should have complained
		false
	else
		echo "happy to see $err"
	fi
'

echo file2 >.gitignore

test_expect_success 'two-way with incorrect --exclude-per-directory (1)' '

	if err=`git read-tree -m --exclude-per-directory=.gitignore master side 2>&1`
	then
		echo should have complained
		false
	else
		echo "happy to see $err"
	fi
'

test_expect_success 'two-way with incorrect --exclude-per-directory (2)' '

	if err=`git read-tree -m -u --exclude-per-directory=foo --exclude-per-directory=.gitignore master side 2>&1`
	then
		echo should have complained
		false
	else
		echo "happy to see $err"
	fi
'

test_expect_success 'two-way clobbering a ignored file' '

	git read-tree -m -u --exclude-per-directory=.gitignore master side
'

rm -f .gitignore

# three-tree test

test_expect_success 'three-way not complaining on an untracked path in both' '

	rm -f file2 subdir/file2 &&
	git checkout side &&
	echo >file3 file three &&
	echo >subdir/file3 file three &&
	git update-index --add file3 subdir/file3 &&
	git commit -a -m "side adds file3 and removes file2" &&

	git checkout master &&
	echo >file2 file two is untracked on the master side &&
	echo >subdir/file2 file two is untracked on the master side &&

	git-read-tree -m -u branch-point master side
'

test_expect_success 'three-way not cloberring a working tree file' '

	git reset --hard &&
	rm -f file2 subdir/file2 file3 subdir/file3 &&
	git checkout master &&
	echo >file3 file three created in master, untracked &&
	echo >subdir/file3 file three created in master, untracked &&
	if err=`git read-tree -m -u branch-point master side 2>&1`
	then
		echo should have complained
		false
	else
		echo "happy to see $err"
	fi
'

echo >.gitignore file3

test_expect_success 'three-way not complaining on an untracked file' '

	git reset --hard &&
	rm -f file2 subdir/file2 file3 subdir/file3 &&
	git checkout master &&
	echo >file3 file three created in master, untracked &&
	echo >subdir/file3 file three created in master, untracked &&

	git read-tree -m -u --exclude-per-directory=.gitignore branch-point master side
'

test_done
