#!/bin/sh

test_description='but rebase + directory rename tests'

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
		but add x l &&
		but cummit -m "Initial" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		but mv x y &&
		but mv l letters &&
		but cummit -m "Rename x to y, l to letters" &&

		but checkout B &&
		echo j >>l &&
		test_seq 31 40 >x/d &&
		but add l x/d &&
		but cummit -m "Modify l, add x/d"
	)
'

test_expect_success 'rebase --interactive: directory rename detected' '
	(
		cd dir-rename &&

		but checkout B^0 &&

		set_fake_editor &&
		FAKE_LINES="1" but -c merge.directoryRenames=true rebase --interactive A &&

		but ls-files -s >out &&
		test_line_count = 5 out &&

		test_path_is_file y/d &&
		test_path_is_missing x/d
	)
'

test_expect_failure 'rebase --apply: directory rename detected' '
	(
		cd dir-rename &&

		but checkout B^0 &&

		but -c merge.directoryRenames=true rebase --apply A &&

		but ls-files -s >out &&
		test_line_count = 5 out &&

		test_path_is_file y/d &&
		test_path_is_missing x/d
	)
'

test_expect_success 'rebase --merge: directory rename detected' '
	(
		cd dir-rename &&

		but checkout B^0 &&

		but -c merge.directoryRenames=true rebase --merge A &&

		but ls-files -s >out &&
		test_line_count = 5 out &&

		test_path_is_file y/d &&
		test_path_is_missing x/d
	)
'

test_expect_failure 'am: directory rename detected' '
	(
		cd dir-rename &&

		but checkout A^0 &&

		but format-patch -1 B &&

		but -c merge.directoryRenames=true am --3way 0001*.patch &&

		but ls-files -s >out &&
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
		but add x project_info &&
		but cummit -m "Initial" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		echo v2 >project_info &&
		but add project_info &&
		but cummit -m "Modify project_info" &&

		but checkout B &&
		mkdir y &&
		but mv x/c y/c &&
		echo v1 >project_info &&
		but add project_info &&
		but cummit -m "Rename x/c to y/c, modify project_info"
	)
'

test_expect_success 'rebase --interactive: NO directory rename' '
	test_when_finished "but -C no-dir-rename rebase --abort" &&
	(
		cd no-dir-rename &&

		but checkout B^0 &&

		set_fake_editor &&
		test_must_fail env FAKE_LINES="1" but rebase --interactive A &&

		but ls-files -s >out &&
		test_line_count = 6 out &&

		test_path_is_file x/a &&
		test_path_is_file x/b &&
		test_path_is_missing x/c
	)
'

test_expect_success 'rebase (am): NO directory rename' '
	test_when_finished "but -C no-dir-rename rebase --abort" &&
	(
		cd no-dir-rename &&

		but checkout B^0 &&

		set_fake_editor &&
		test_must_fail but rebase A &&

		but ls-files -s >out &&
		test_line_count = 6 out &&

		test_path_is_file x/a &&
		test_path_is_file x/b &&
		test_path_is_missing x/c
	)
'

test_expect_success 'rebase --merge: NO directory rename' '
	test_when_finished "but -C no-dir-rename rebase --abort" &&
	(
		cd no-dir-rename &&

		but checkout B^0 &&

		set_fake_editor &&
		test_must_fail but rebase --merge A &&

		but ls-files -s >out &&
		test_line_count = 6 out &&

		test_path_is_file x/a &&
		test_path_is_file x/b &&
		test_path_is_missing x/c
	)
'

test_expect_success 'am: NO directory rename' '
	test_when_finished "but -C no-dir-rename am --abort" &&
	(
		cd no-dir-rename &&

		but checkout A^0 &&

		but format-patch -1 B &&

		test_must_fail but am --3way 0001*.patch &&

		but ls-files -s >out &&
		test_line_count = 6 out &&

		test_path_is_file x/a &&
		test_path_is_file x/b &&
		test_path_is_missing x/c
	)
'

test_done
