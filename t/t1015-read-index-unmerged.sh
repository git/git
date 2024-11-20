#!/bin/sh

test_description='Test various callers of read_index_unmerged'

. ./test-lib.sh

test_expect_success 'setup modify/delete + directory/file conflict' '
	test_create_repo df_plus_modify_delete &&
	(
		cd df_plus_modify_delete &&

		test_write_lines a b c d e f g h >letters &&
		git add letters &&
		git commit -m initial &&

		git checkout -b modify &&
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
	)
'

test_expect_success 'read-tree --reset cleans unmerged entries' '
	test_when_finished "git -C df_plus_modify_delete clean -f" &&
	test_when_finished "git -C df_plus_modify_delete reset --hard" &&
	(
		cd df_plus_modify_delete &&

		git checkout delete^0 &&
		test_must_fail git merge modify &&

		git read-tree --reset HEAD &&
		git ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

test_expect_success 'One reset --hard cleans unmerged entries' '
	test_when_finished "git -C df_plus_modify_delete clean -f" &&
	test_when_finished "git -C df_plus_modify_delete reset --hard" &&
	(
		cd df_plus_modify_delete &&

		git checkout delete^0 &&
		test_must_fail git merge modify &&

		git reset --hard &&
		test_path_is_missing .git/MERGE_HEAD &&
		git ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

test_expect_success 'setup directory/file conflict + simple edit/edit' '
	test_create_repo df_plus_edit_edit &&
	(
		cd df_plus_edit_edit &&

		test_seq 1 10 >numbers &&
		git add numbers &&
		git commit -m initial &&

		git checkout -b d-edit &&
		mkdir foo &&
		echo content >foo/bar &&
		git add foo &&
		echo 11 >>numbers &&
		git add numbers &&
		git commit -m "directory and edit" &&

		git checkout -b f-edit d-edit^1 &&
		echo content >foo &&
		git add foo &&
		echo eleven >>numbers &&
		git add numbers &&
		git commit -m "file and edit"
	)
'

test_expect_success 'git merge --abort succeeds despite D/F conflict' '
	test_when_finished "git -C df_plus_edit_edit clean -f" &&
	test_when_finished "git -C df_plus_edit_edit reset --hard" &&
	(
		cd df_plus_edit_edit &&

		git checkout f-edit^0 &&
		test_must_fail git merge d-edit^0 &&

		git merge --abort &&
		test_path_is_missing .git/MERGE_HEAD &&
		git ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

test_expect_success 'git am --skip succeeds despite D/F conflict' '
	test_when_finished "git -C df_plus_edit_edit clean -f" &&
	test_when_finished "git -C df_plus_edit_edit reset --hard" &&
	(
		cd df_plus_edit_edit &&

		git checkout f-edit^0 &&
		git format-patch -1 d-edit &&
		test_must_fail git am -3 0001*.patch &&

		git am --skip &&
		test_path_is_missing .git/rebase-apply &&
		git ls-files -u >conflicts &&
		test_must_be_empty conflicts
	)
'

test_done
