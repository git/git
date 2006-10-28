#!/bin/sh

test_description='read-tree -m -u checks working tree files'

. ./test-lib.sh

# two-tree test

test_expect_success 'two-way setup' '

	echo >file1 file one &&
	echo >file2 file two &&
	git update-index --add file1 file2 &&
	git commit -m initial &&

	git branch side &&
	git tag -f branch-point &&

	echo file2 is not tracked on the master anymore &&
	rm -f file2 &&
	git update-index --remove file2 &&
	git commit -a -m "master removes file2"
'

test_expect_success 'two-way not clobbering' '

	echo >file2 master creates untracked file2 &&
	if err=`git read-tree -m -u master side 2>&1`
	then
		echo should have complained
		false
	else
		echo "happy to see $err"
	fi
'

# three-tree test

test_expect_success 'three-way not complaining' '

	rm -f file2 &&
	git checkout side &&
	echo >file3 file three &&
	git update-index --add file3 &&
	git commit -a -m "side adds file3" &&

	git checkout master &&
	echo >file2 file two is untracked on the master side &&

	git-read-tree -m -u branch-point master side
'

test_done
