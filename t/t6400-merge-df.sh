#!/bin/sh
#
# Copyright (c) 2005 Fredrik Kuivinen
#

test_description='Test merge with directory/file conflicts'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
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
	test_expect_code 1 git merge -m "merge msg" main
'

test_expect_success 'F/D conflict' '
	git reset --hard &&
	git checkout main &&
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

	git merge main
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

	test_stdout_line_count = 5 git ls-files -s &&
	test_stdout_line_count = 4 git ls-files -u &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test_stdout_line_count = 0 git ls-files -o
	else
		test_stdout_line_count = 1 git ls-files -o
	fi &&

	test_path_is_file letters/file &&
	test_path_is_file letters.txt &&
	test_path_is_file letters~modify
'

test_expect_success 'modify/delete + directory/file conflict; other way' '
	git reset --hard &&
	git clean -f &&
	git checkout modify^0 &&

	test_must_fail git merge delete &&

	test_stdout_line_count = 5 git ls-files -s &&
	test_stdout_line_count = 4 git ls-files -u &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test_stdout_line_count = 0 git ls-files -o
	else
		test_stdout_line_count = 1 git ls-files -o
	fi &&

	test_path_is_file letters/file &&
	test_path_is_file letters.txt &&
	test_path_is_file letters~HEAD
'

test_expect_success 'Simple merge in repo with interesting pathnames' '
	# Simple lexicographic ordering of files and directories would be:
	#     foo
	#     foo/bar
	#     foo/bar-2
	#     foo/bar/baz
	#     foo/bar-2/baz
	# The fact that foo/bar-2 appears between foo/bar and foo/bar/baz
	# can trip up some codepaths, and is the point of this test.
	git init name-ordering &&
	(
		cd name-ordering &&

		mkdir -p foo/bar &&
		mkdir -p foo/bar-2 &&
		>foo/bar/baz &&
		>foo/bar-2/baz &&
		git add . &&
		git commit -m initial &&

		git branch topic &&
		git branch other &&

		git checkout other &&
		echo other >foo/bar-2/baz &&
		git add -u &&
		git commit -m other &&

		git checkout topic &&
		echo topic >foo/bar/baz &&
		git add -u &&
		git commit -m topic &&

		git merge other &&
		git ls-files -s >out &&
		test_line_count = 2 out &&
		git rev-parse :0:foo/bar/baz :0:foo/bar-2/baz >actual &&
		git rev-parse HEAD~1:foo/bar/baz other:foo/bar-2/baz >expect &&
		test_cmp expect actual
	)

'

test_done
