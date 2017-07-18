#!/bin/sh
#
# Copyright (c) 2005 Fredrik Kuivinen
#

test_description='Test merge with directory/file conflicts'
. ./test-lib.sh

test_expect_success 'prepare repository' '
	echo Hello >init &&
	git add init &&
	git commit -m initial &&

	git branch B &&
	mkdir dir &&
	echo foo >dir/foo &&
	git add dir/foo &&
	git commit -m "File: dir/foo" &&

	git checkout B &&
	echo file dir >dir &&
	git add dir &&
	git commit -m "File: dir"
'

test_expect_success 'Merge with d/f conflicts' '
	test_expect_code 1 git merge -m "merge msg" master
'

test_expect_success 'F/D conflict' '
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

test_expect_success 'setup modify/delete + directory/file conflict' '
	git checkout --orphan modify &&
	git rm -rf . &&
	git clean -fdqx &&

	printf "a\nb\nc\nd\ne\nf\ng\nh\n" >letters &&
	git add letters &&
	git commit -m initial &&

	# Throw in letters.txt for sorting order fun
	# ("letters.txt" sorts between "letters" and "letters/file")
	echo i >>letters &&
	echo "version 2" >letters.txt &&
	git add letters letters.txt &&
	git commit -m modified &&

	git checkout -b delete HEAD^ &&
	git rm letters &&
	mkdir letters &&
	>letters/file &&
	echo "version 1" >letters.txt &&
	git add letters letters.txt &&
	git commit -m deleted
'

test_expect_success 'modify/delete + directory/file conflict' '
	git checkout delete^0 &&
	test_must_fail git merge modify &&

	test 5 -eq $(git ls-files -s | wc -l) &&
	test 4 -eq $(git ls-files -u | wc -l) &&
	test 1 -eq $(git ls-files -o | wc -l) &&

	test -f letters/file &&
	test -f letters.txt &&
	test -f letters~modify
'

test_expect_success 'modify/delete + directory/file conflict; other way' '
	# Yes, we really need the double reset since "letters" appears as
	# both a file and a directory.
	git reset --hard &&
	git reset --hard &&
	git clean -f &&
	git checkout modify^0 &&

	test_must_fail git merge delete &&

	test 5 -eq $(git ls-files -s | wc -l) &&
	test 4 -eq $(git ls-files -u | wc -l) &&
	test 1 -eq $(git ls-files -o | wc -l) &&

	test -f letters/file &&
	test -f letters.txt &&
	test -f letters~HEAD
'

test_done
