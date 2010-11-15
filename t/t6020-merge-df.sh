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
	test_expect_code 1 git merge "merge msg" B master
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

	echo i >>letters &&
	git add letters &&
	git commit -m modified &&

	git checkout -b delete HEAD^ &&
	git rm letters &&
	mkdir letters &&
	>letters/file &&
	git add letters &&
	git commit -m deleted
'

test_expect_success 'modify/delete + directory/file conflict' '
	git checkout delete^0 &&
	test_must_fail git merge modify &&

	test 3 = $(git ls-files -s | wc -l) &&
	test 2 = $(git ls-files -u | wc -l) &&
	test 1 = $(git ls-files -o | wc -l) &&

	test -f letters/file &&
	test -f letters~modify
'

test_expect_success 'modify/delete + directory/file conflict; other way' '
	git reset --hard &&
	git clean -f &&
	git checkout modify^0 &&
	test_must_fail git merge delete &&

	test 3 = $(git ls-files -s | wc -l) &&
	test 2 = $(git ls-files -u | wc -l) &&
	test 1 = $(git ls-files -o | wc -l) &&

	test -f letters/file &&
	test -f letters~HEAD
'

test_done
