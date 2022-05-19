#!/bin/sh
#
# Copyright (c) 2005 Fredrik Kuivinen
#

test_description='Test merge with directory/file conflicts'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'prepare repository' '
	echo Hello >init &&
	but add init &&
	but cummit -m initial &&

	but branch B &&
	mkdir dir &&
	echo foo >dir/foo &&
	but add dir/foo &&
	but cummit -m "File: dir/foo" &&

	but checkout B &&
	echo file dir >dir &&
	but add dir &&
	but cummit -m "File: dir"
'

test_expect_success 'Merge with d/f conflicts' '
	test_expect_code 1 but merge -m "merge msg" main
'

test_expect_success 'F/D conflict' '
	but reset --hard &&
	but checkout main &&
	rm .but/index &&

	mkdir before &&
	echo FILE >before/one &&
	echo FILE >after &&
	but add . &&
	but cummit -m first &&

	rm -f after &&
	but mv before after &&
	but cummit -m move &&

	but checkout -b para HEAD^ &&
	echo COMPLETELY ANOTHER FILE >another &&
	but add . &&
	but cummit -m para &&

	but merge main
'

test_expect_success 'setup modify/delete + directory/file conflict' '
	but checkout --orphan modify &&
	but rm -rf . &&
	but clean -fdqx &&

	printf "a\nb\nc\nd\ne\nf\ng\nh\n" >letters &&
	but add letters &&
	but cummit -m initial &&

	# Throw in letters.txt for sorting order fun
	# ("letters.txt" sorts between "letters" and "letters/file")
	echo i >>letters &&
	echo "version 2" >letters.txt &&
	but add letters letters.txt &&
	but cummit -m modified &&

	but checkout -b delete HEAD^ &&
	but rm letters &&
	mkdir letters &&
	>letters/file &&
	echo "version 1" >letters.txt &&
	but add letters letters.txt &&
	but cummit -m deleted
'

test_expect_success 'modify/delete + directory/file conflict' '
	but checkout delete^0 &&
	test_must_fail but merge modify &&

	test_stdout_line_count = 5 but ls-files -s &&
	test_stdout_line_count = 4 but ls-files -u &&
	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_stdout_line_count = 0 but ls-files -o
	else
		test_stdout_line_count = 1 but ls-files -o
	fi &&

	test_path_is_file letters/file &&
	test_path_is_file letters.txt &&
	test_path_is_file letters~modify
'

test_expect_success 'modify/delete + directory/file conflict; other way' '
	but reset --hard &&
	but clean -f &&
	but checkout modify^0 &&

	test_must_fail but merge delete &&

	test_stdout_line_count = 5 but ls-files -s &&
	test_stdout_line_count = 4 but ls-files -u &&
	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_stdout_line_count = 0 but ls-files -o
	else
		test_stdout_line_count = 1 but ls-files -o
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
	test_create_repo name-ordering &&
	(
		cd name-ordering &&

		mkdir -p foo/bar &&
		mkdir -p foo/bar-2 &&
		>foo/bar/baz &&
		>foo/bar-2/baz &&
		but add . &&
		but cummit -m initial &&

		but branch topic &&
		but branch other &&

		but checkout other &&
		echo other >foo/bar-2/baz &&
		but add -u &&
		but cummit -m other &&

		but checkout topic &&
		echo topic >foo/bar/baz &&
		but add -u &&
		but cummit -m topic &&

		but merge other &&
		but ls-files -s >out &&
		test_line_count = 2 out &&
		but rev-parse :0:foo/bar/baz :0:foo/bar-2/baz >actual &&
		but rev-parse HEAD~1:foo/bar/baz other:foo/bar-2/baz >expect &&
		test_cmp expect actual
	)

'

test_done
