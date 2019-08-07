#!/bin/sh

test_description='git rebase + directory rename tests'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup testcase where directory rename should be detected' '
	test_create_repo dir-rename &&
	(
		cd dir-rename &&

		mkdir x &&
		test_seq  1 10 >x/a &&
		test_seq 11 20 >x/b &&
		test_seq 21 30 >x/c &&
		test_write_lines a b c d e f g h i >l &&
		git add x l &&
		git commit -m "Initial" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv x y &&
		git mv l letters &&
		git commit -m "Rename x to y, l to letters" &&

		git checkout B &&
		echo j >>l &&
		test_seq 31 40 >x/d &&
		git add l x/d &&
		git commit -m "Modify l, add x/d"
	)
'

test_expect_success 'rebase --interactive: directory rename detected' '
	(
		cd dir-rename &&

		git checkout B^0 &&

		set_fake_editor &&
		FAKE_LINES="1" git -c merge.directoryRenames=true rebase --interactive A &&

		git ls-files -s >out &&
		test_line_count = 5 out &&

		test_path_is_file y/d &&
		test_path_is_missing x/d
	)
'

test_expect_failure 'rebase (am): directory rename detected' '
	(
		cd dir-rename &&

		git checkout B^0 &&

		git -c merge.directoryRenames=true rebase A &&

		git ls-files -s >out &&
		test_line_count = 5 out &&

		test_path_is_file y/d &&
		test_path_is_missing x/d
	)
'

test_expect_success 'rebase --merge: directory rename detected' '
	(
		cd dir-rename &&

		git checkout B^0 &&

		git -c merge.directoryRenames=true rebase --merge A &&

		git ls-files -s >out &&
		test_line_count = 5 out &&

		test_path_is_file y/d &&
		test_path_is_missing x/d
	)
'

test_expect_failure 'am: directory rename detected' '
	(
		cd dir-rename &&

		git checkout A^0 &&

		git format-patch -1 B &&

		git -c merge.directoryRenames=true am --3way 0001*.patch &&

		git ls-files -s >out &&
		test_line_count = 5 out &&

		test_path_is_file y/d &&
		test_path_is_missing x/d
	)
'

test_expect_success 'setup testcase where directory rename should NOT be detected' '
	test_create_repo no-dir-rename &&
	(
		cd no-dir-rename &&

		mkdir x &&
		test_seq  1 10 >x/a &&
		test_seq 11 20 >x/b &&
		test_seq 21 30 >x/c &&
		echo original >project_info &&
		git add x project_info &&
		git commit -m "Initial" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		echo v2 >project_info &&
		git add project_info &&
		git commit -m "Modify project_info" &&

		git checkout B &&
		mkdir y &&
		git mv x/c y/c &&
		echo v1 >project_info &&
		git add project_info &&
		git commit -m "Rename x/c to y/c, modify project_info"
	)
'

test_expect_success 'rebase --interactive: NO directory rename' '
	test_when_finished "git -C no-dir-rename rebase --abort" &&
	(
		cd no-dir-rename &&

		git checkout B^0 &&

		set_fake_editor &&
		test_must_fail env FAKE_LINES="1" git rebase --interactive A &&

		git ls-files -s >out &&
		test_line_count = 6 out &&

		test_path_is_file x/a &&
		test_path_is_file x/b &&
		test_path_is_missing x/c
	)
'

test_expect_success 'rebase (am): NO directory rename' '
	test_when_finished "git -C no-dir-rename rebase --abort" &&
	(
		cd no-dir-rename &&

		git checkout B^0 &&

		set_fake_editor &&
		test_must_fail git rebase A &&

		git ls-files -s >out &&
		test_line_count = 6 out &&

		test_path_is_file x/a &&
		test_path_is_file x/b &&
		test_path_is_missing x/c
	)
'

test_expect_success 'rebase --merge: NO directory rename' '
	test_when_finished "git -C no-dir-rename rebase --abort" &&
	(
		cd no-dir-rename &&

		git checkout B^0 &&

		set_fake_editor &&
		test_must_fail git rebase --merge A &&

		git ls-files -s >out &&
		test_line_count = 6 out &&

		test_path_is_file x/a &&
		test_path_is_file x/b &&
		test_path_is_missing x/c
	)
'

test_expect_success 'am: NO directory rename' '
	test_when_finished "git -C no-dir-rename am --abort" &&
	(
		cd no-dir-rename &&

		git checkout A^0 &&

		git format-patch -1 B &&

		test_must_fail git am --3way 0001*.patch &&

		git ls-files -s >out &&
		test_line_count = 6 out &&

		test_path_is_file x/a &&
		test_path_is_file x/b &&
		test_path_is_missing x/c
	)
'

test_done
