#!/bin/sh
#
# Copyright (c) 2005 Fredrik Kuivinen
#

test_description='Test merge with directory/file conflicts'
. ./test-lib.sh

test_expect_success 'prepare repository' \
'echo "Hello" > init &&
git add init &&
git commit -m "Initial commit" &&
git branch B &&
mkdir dir &&
echo "foo" > dir/foo &&
git add dir/foo &&
git commit -m "File: dir/foo" &&
git checkout B &&
echo "file dir" > dir &&
git add dir &&
git commit -m "File: dir"'

test_expect_code 1 'Merge with d/f conflicts' 'git merge "merge msg" B master'

test_expect_failure 'F/D conflict' '
	git reset --hard &&
	git checkout master &&
	rm .git/index &&

	mkdir before &&
	echo FILE >before/one &&
	echo FILE >after &&
	git add . &&
	git commit -m first &&

	rm -f after &&
	git mv before after &&
	git commit -m move &&

	git checkout -b para HEAD^ &&
	echo COMPLETELY ANOTHER FILE >another &&
	git add . &&
	git commit -m para &&

	git merge master
'

test_done
