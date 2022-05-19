#!/bin/sh

test_description='Test various callers of read_index_unmerged'
. ./test-lib.sh

test_expect_success 'setup modify/delete + directory/file conflict' '
	test_create_repo df_plus_modify_delete &&
	(
		cd df_plus_modify_delete &&

		test_write_lines a b c d e f g h >letters &&
		but add letters &&
		but cummit -m initial &&

		but checkout -b modify &&
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
	)
'

test_expect_success 'read-tree --reset cleans unmerged entries' '
	test_when_finished "but -C df_plus_modify_delete clean -f" &&
	test_when_finished "but -C df_plus_modify_delete reset --hard" &&
	(
		cd df_plus_modify_delete &&

		but checkout delete^0 &&
		test_must_fail but merge modify &&

		but read-tree --reset HEAD &&
		but ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

test_expect_success 'One reset --hard cleans unmerged entries' '
	test_when_finished "but -C df_plus_modify_delete clean -f" &&
	test_when_finished "but -C df_plus_modify_delete reset --hard" &&
	(
		cd df_plus_modify_delete &&

		but checkout delete^0 &&
		test_must_fail but merge modify &&

		but reset --hard &&
		test_path_is_missing .but/MERGE_HEAD &&
		but ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

test_expect_success 'setup directory/file conflict + simple edit/edit' '
	test_create_repo df_plus_edit_edit &&
	(
		cd df_plus_edit_edit &&

		test_seq 1 10 >numbers &&
		but add numbers &&
		but cummit -m initial &&

		but checkout -b d-edit &&
		mkdir foo &&
		echo content >foo/bar &&
		but add foo &&
		echo 11 >>numbers &&
		but add numbers &&
		but cummit -m "directory and edit" &&

		but checkout -b f-edit d-edit^1 &&
		echo content >foo &&
		but add foo &&
		echo eleven >>numbers &&
		but add numbers &&
		but cummit -m "file and edit"
	)
'

test_expect_success 'but merge --abort succeeds despite D/F conflict' '
	test_when_finished "but -C df_plus_edit_edit clean -f" &&
	test_when_finished "but -C df_plus_edit_edit reset --hard" &&
	(
		cd df_plus_edit_edit &&

		but checkout f-edit^0 &&
		test_must_fail but merge d-edit^0 &&

		but merge --abort &&
		test_path_is_missing .but/MERGE_HEAD &&
		but ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

test_expect_success 'but am --skip succeeds despite D/F conflict' '
	test_when_finished "but -C df_plus_edit_edit clean -f" &&
	test_when_finished "but -C df_plus_edit_edit reset --hard" &&
	(
		cd df_plus_edit_edit &&

		but checkout f-edit^0 &&
		but format-patch -1 d-edit &&
		test_must_fail but am -3 0001*.patch &&

		but am --skip &&
		test_path_is_missing .but/rebase-apply &&
		but ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

test_done
