#!/bin/sh

test_description="recursive merge corner cases w/ renames but not criss-crosses"
# t6036 has corner cases that involve both criss-cross merges and renames

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-merge.sh

test_setup_rename_delete_untracked () {
	test_create_repo rename-delete-untracked &&
	(
		cd rename-delete-untracked &&

		echo "A pretty inscription" >ring &&
		but add ring &&
		test_tick &&
		but cummit -m beginning &&

		but branch people &&
		but checkout -b rename-the-ring &&
		but mv ring one-ring-to-rule-them-all &&
		test_tick &&
		but cummit -m fullname &&

		but checkout people &&
		but rm ring &&
		echo gollum >owner &&
		but add owner &&
		test_tick &&
		but cummit -m track-people-instead-of-objects &&
		echo "Myyy PRECIOUSSS" >ring
	)
}

test_expect_success "Does but preserve Gollum's precious artifact?" '
	test_setup_rename_delete_untracked &&
	(
		cd rename-delete-untracked &&

		test_must_fail but merge -s recursive rename-the-ring &&

		# Make sure but did not delete an untracked file
		test_path_is_file ring
	)
'

# Testcase setup for rename/modify/add-source:
#   cummit A: new file: a
#   cummit B: modify a slightly
#   cummit C: rename a->b, add completely different a
#
# We should be able to merge B & C cleanly

test_setup_rename_modify_add_source () {
	test_create_repo rename-modify-add-source &&
	(
		cd rename-modify-add-source &&

		printf "1\n2\n3\n4\n5\n6\n7\n" >a &&
		but add a &&
		but cummit -m A &&
		but tag A &&

		but checkout -b B A &&
		echo 8 >>a &&
		but add a &&
		but cummit -m B &&

		but checkout -b C A &&
		but mv a b &&
		echo something completely different >a &&
		but add a &&
		but cummit -m C
	)
}

test_expect_failure 'rename/modify/add-source conflict resolvable' '
	test_setup_rename_modify_add_source &&
	(
		cd rename-modify-add-source &&

		but checkout B^0 &&

		but merge -s recursive C^0 &&

		but rev-parse >expect \
			B:a   C:a     &&
		but rev-parse >actual \
			b     c       &&
		test_cmp expect actual
	)
'

test_setup_break_detection_1 () {
	test_create_repo break-detection-1 &&
	(
		cd break-detection-1 &&

		printf "1\n2\n3\n4\n5\n" >a &&
		echo foo >b &&
		but add a b &&
		but cummit -m A &&
		but tag A &&

		but checkout -b B A &&
		but mv a c &&
		echo "Completely different content" >a &&
		but add a &&
		but cummit -m B &&

		but checkout -b C A &&
		echo 6 >>a &&
		but add a &&
		but cummit -m C
	)
}

test_expect_failure 'conflict caused if rename not detected' '
	test_setup_break_detection_1 &&
	(
		cd break-detection-1 &&

		but checkout -q C^0 &&
		but merge -s recursive B^0 &&

		but ls-files -s >out &&
		test_line_count = 3 out &&
		but ls-files -u >out &&
		test_line_count = 0 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		test_line_count = 6 c &&
		but rev-parse >expect \
			B:a   A:b     &&
		but rev-parse >actual \
			:0:a  :0:b    &&
		test_cmp expect actual
	)
'

test_setup_break_detection_2 () {
	test_create_repo break-detection-2 &&
	(
		cd break-detection-2 &&

		printf "1\n2\n3\n4\n5\n" >a &&
		echo foo >b &&
		but add a b &&
		but cummit -m A &&
		but tag A &&

		but checkout -b D A &&
		echo 7 >>a &&
		but add a &&
		but mv a c &&
		echo "Completely different content" >a &&
		but add a &&
		but cummit -m D &&

		but checkout -b E A &&
		but rm a &&
		echo "Completely different content" >>a &&
		but add a &&
		but cummit -m E
	)
}

test_expect_failure 'missed conflict if rename not detected' '
	test_setup_break_detection_2 &&
	(
		cd break-detection-2 &&

		but checkout -q E^0 &&
		test_must_fail but merge -s recursive D^0
	)
'

# Tests for undetected rename/add-source causing a file to erroneously be
# deleted (and for mishandled rename/rename(1to1) causing the same issue).
#
# This test uses a rename/rename(1to1)+add-source conflict (1to1 means the
# same file is renamed on both sides to the same thing; it should trigger
# the 1to2 logic, which it would do if the add-source didn't cause issues
# for but's rename detection):
#   cummit A: new file: a
#   cummit B: rename a->b
#   cummit C: rename a->b, add unrelated a

test_setup_break_detection_3 () {
	test_create_repo break-detection-3 &&
	(
		cd break-detection-3 &&

		printf "1\n2\n3\n4\n5\n" >a &&
		but add a &&
		but cummit -m A &&
		but tag A &&

		but checkout -b B A &&
		but mv a b &&
		but cummit -m B &&

		but checkout -b C A &&
		but mv a b &&
		echo foobar >a &&
		but add a &&
		but cummit -m C
	)
}

test_expect_failure 'detect rename/add-source and preserve all data' '
	test_setup_break_detection_3 &&
	(
		cd break-detection-3 &&

		but checkout B^0 &&

		but merge -s recursive C^0 &&

		but ls-files -s >out &&
		test_line_count = 2 out &&
		but ls-files -u >out &&
		test_line_count = 2 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_file a &&
		test_path_is_file b &&

		but rev-parse >expect \
			A:a   C:a     &&
		but rev-parse >actual \
			:0:b  :0:a    &&
		test_cmp expect actual
	)
'

test_expect_failure 'detect rename/add-source and preserve all data, merge other way' '
	test_setup_break_detection_3 &&
	(
		cd break-detection-3 &&

		but checkout C^0 &&

		but merge -s recursive B^0 &&

		but ls-files -s >out &&
		test_line_count = 2 out &&
		but ls-files -u >out &&
		test_line_count = 2 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_file a &&
		test_path_is_file b &&

		but rev-parse >expect \
			A:a   C:a     &&
		but rev-parse >actual \
			:0:b  :0:a    &&
		test_cmp expect actual
	)
'

test_setup_rename_directory () {
	test_create_repo rename-directory-$1 &&
	(
		cd rename-directory-$1 &&

		printf "1\n2\n3\n4\n5\n6\n" >file &&
		but add file &&
		test_tick &&
		but cummit -m base &&
		but tag base &&

		but checkout -b right &&
		echo 7 >>file &&
		mkdir newfile &&
		echo junk >newfile/realfile &&
		but add file newfile/realfile &&
		test_tick &&
		but cummit -m right &&

		but checkout -b left-conflict base &&
		echo 8 >>file &&
		but add file &&
		but mv file newfile &&
		test_tick &&
		but cummit -m left &&

		but checkout -b left-clean base &&
		echo 0 >newfile &&
		cat file >>newfile &&
		but add newfile &&
		but rm file &&
		test_tick &&
		but cummit -m left
	)
}

test_expect_success 'rename/directory conflict + clean content merge' '
	test_setup_rename_directory 1a &&
	(
		cd rename-directory-1a &&

		but checkout left-clean^0 &&

		test_must_fail but merge -s recursive right^0 &&

		but ls-files -s >out &&
		test_line_count = 2 out &&
		but ls-files -u >out &&
		test_line_count = 1 out &&
		but ls-files -o >out &&
		if test "$BUT_TEST_MERGE_ALGORITHM" = ort
		then
			test_line_count = 1 out
		else
			test_line_count = 2 out
		fi &&

		echo 0 >expect &&
		but cat-file -p base:file >>expect &&
		echo 7 >>expect &&
		test_cmp expect newfile~HEAD &&

		test_path_is_file newfile/realfile &&
		test_path_is_file newfile~HEAD
	)
'

test_expect_success 'rename/directory conflict + content merge conflict' '
	test_setup_rename_directory 1b &&
	(
		cd rename-directory-1b &&

		but reset --hard &&
		but clean -fdqx &&

		but checkout left-conflict^0 &&

		test_must_fail but merge -s recursive right^0 &&

		but ls-files -s >out &&
		test_line_count = 4 out &&
		but ls-files -u >out &&
		test_line_count = 3 out &&
		but ls-files -o >out &&
		if test "$BUT_TEST_MERGE_ALGORITHM" = ort
		then
			test_line_count = 1 out
		else
			test_line_count = 2 out
		fi &&

		but cat-file -p left-conflict:newfile >left &&
		but cat-file -p base:file    >base &&
		but cat-file -p right:file   >right &&
		test_must_fail but merge-file \
			-L "HEAD:newfile" \
			-L "" \
			-L "right^0:file" \
			left base right &&
		test_cmp left newfile~HEAD &&

		but rev-parse >expect   \
			base:file       left-conflict:newfile right:file &&
		if test "$BUT_TEST_MERGE_ALGORITHM" = ort
		then
			but rev-parse >actual \
				:1:newfile~HEAD :2:newfile~HEAD :3:newfile~HEAD
		else
			but rev-parse >actual \
				:1:newfile      :2:newfile      :3:newfile
		fi &&
		test_cmp expect actual &&

		test_path_is_file newfile/realfile &&
		test_path_is_file newfile~HEAD
	)
'

test_setup_rename_directory_2 () {
	test_create_repo rename-directory-2 &&
	(
		cd rename-directory-2 &&

		mkdir sub &&
		printf "1\n2\n3\n4\n5\n6\n" >sub/file &&
		but add sub/file &&
		test_tick &&
		but cummit -m base &&
		but tag base &&

		but checkout -b right &&
		echo 7 >>sub/file &&
		but add sub/file &&
		test_tick &&
		but cummit -m right &&

		but checkout -b left base &&
		echo 0 >newfile &&
		cat sub/file >>newfile &&
		but rm sub/file &&
		mv newfile sub &&
		but add sub &&
		test_tick &&
		but cummit -m left
	)
}

test_expect_success 'disappearing dir in rename/directory conflict handled' '
	test_setup_rename_directory_2 &&
	(
		cd rename-directory-2 &&

		but checkout left^0 &&

		but merge -s recursive right^0 &&

		but ls-files -s >out &&
		test_line_count = 1 out &&
		but ls-files -u >out &&
		test_line_count = 0 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		echo 0 >expect &&
		but cat-file -p base:sub/file >>expect &&
		echo 7 >>expect &&
		test_cmp expect sub &&

		test_path_is_file sub
	)
'

# Test for basic rename/add-dest conflict, with rename needing content merge:
#   cummit O: a
#   cummit A: rename a->b, modifying b too
#   cummit B: modify a, add different b

test_setup_rename_with_content_merge_and_add () {
	test_create_repo rename-with-content-merge-and-add-$1 &&
	(
		cd rename-with-content-merge-and-add-$1 &&

		test_seq 1 5 >a &&
		but add a &&
		but cummit -m O &&
		but tag O &&

		but checkout -b A O &&
		but mv a b &&
		test_seq 0 5 >b &&
		but add b &&
		but cummit -m A &&

		but checkout -b B O &&
		echo 6 >>a &&
		echo hello world >b &&
		but add a b &&
		but cummit -m B
	)
}

test_expect_success 'handle rename-with-content-merge vs. add' '
	test_setup_rename_with_content_merge_and_add AB &&
	(
		cd rename-with-content-merge-and-add-AB &&

		but checkout A^0 &&

		test_must_fail but merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (.*/add)" out &&

		but ls-files -s >out &&
		test_line_count = 2 out &&
		but ls-files -u >out &&
		test_line_count = 2 out &&
		# Also, make sure both unmerged entries are for "b"
		but ls-files -u b >out &&
		test_line_count = 2 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_missing a &&
		test_path_is_file b &&

		test_seq 0 6 >tmp &&
		but hash-object tmp >expect &&
		but rev-parse B:b >>expect &&
		but rev-parse >actual  \
			:2:b    :3:b   &&
		test_cmp expect actual &&

		# Test that the two-way merge in b is as expected
		but cat-file -p :2:b >>ours &&
		but cat-file -p :3:b >>theirs &&
		>empty &&
		test_must_fail but merge-file \
			-L "HEAD" \
			-L "" \
			-L "B^0" \
			ours empty theirs &&
		test_cmp ours b
	)
'

test_expect_success 'handle rename-with-content-merge vs. add, merge other way' '
	test_setup_rename_with_content_merge_and_add BA &&
	(
		cd rename-with-content-merge-and-add-BA &&

		but reset --hard &&
		but clean -fdx &&

		but checkout B^0 &&

		test_must_fail but merge -s recursive A^0 >out &&
		test_i18ngrep "CONFLICT (.*/add)" out &&

		but ls-files -s >out &&
		test_line_count = 2 out &&
		but ls-files -u >out &&
		test_line_count = 2 out &&
		# Also, make sure both unmerged entries are for "b"
		but ls-files -u b >out &&
		test_line_count = 2 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_missing a &&
		test_path_is_file b &&

		test_seq 0 6 >tmp &&
		but rev-parse B:b >expect &&
		but hash-object tmp >>expect &&
		but rev-parse >actual  \
			:2:b    :3:b   &&
		test_cmp expect actual &&

		# Test that the two-way merge in b is as expected
		but cat-file -p :2:b >>ours &&
		but cat-file -p :3:b >>theirs &&
		>empty &&
		test_must_fail but merge-file \
			-L "HEAD" \
			-L "" \
			-L "A^0" \
			ours empty theirs &&
		test_cmp ours b
	)
'

# Test for all kinds of things that can go wrong with rename/rename (2to1):
#   cummit A: new files: a & b
#   cummit B: rename a->c, modify b
#   cummit C: rename b->c, modify a
#
# Merging of B & C should NOT be clean.  Questions:
#   * Both a & b should be removed by the merge; are they?
#   * The two c's should contain modifications to a & b; do they?
#   * The index should contain two files, both for c; does it?
#   * The working copy should have two files, both of form c~<unique>; does it?
#   * Nothing else should be present.  Is anything?

test_setup_rename_rename_2to1 () {
	test_create_repo rename-rename-2to1 &&
	(
		cd rename-rename-2to1 &&

		printf "1\n2\n3\n4\n5\n" >a &&
		printf "5\n4\n3\n2\n1\n" >b &&
		but add a b &&
		but cummit -m A &&
		but tag A &&

		but checkout -b B A &&
		but mv a c &&
		echo 0 >>b &&
		but add b &&
		but cummit -m B &&

		but checkout -b C A &&
		but mv b c &&
		echo 6 >>a &&
		but add a &&
		but cummit -m C
	)
}

test_expect_success 'handle rename/rename (2to1) conflict correctly' '
	test_setup_rename_rename_2to1 &&
	(
		cd rename-rename-2to1 &&

		but checkout B^0 &&

		test_must_fail but merge -s recursive C^0 >out &&
		test_i18ngrep "CONFLICT (\(.*\)/\1)" out &&

		but ls-files -s >out &&
		test_line_count = 2 out &&
		but ls-files -u >out &&
		test_line_count = 2 out &&
		but ls-files -u c >out &&
		test_line_count = 2 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_missing a &&
		test_path_is_missing b &&

		but rev-parse >expect  \
			C:a     B:b    &&
		but rev-parse >actual  \
			:2:c    :3:c   &&
		test_cmp expect actual &&

		# Test that the two-way merge in new_a is as expected
		but cat-file -p :2:c >>ours &&
		but cat-file -p :3:c >>theirs &&
		>empty &&
		test_must_fail but merge-file \
			-L "HEAD" \
			-L "" \
			-L "C^0" \
			ours empty theirs &&
		but hash-object c >actual &&
		but hash-object ours >expect &&
		test_cmp expect actual
	)
'

# Testcase setup for simple rename/rename (1to2) conflict:
#   cummit A: new file: a
#   cummit B: rename a->b
#   cummit C: rename a->c
test_setup_rename_rename_1to2 () {
	test_create_repo rename-rename-1to2 &&
	(
		cd rename-rename-1to2 &&

		echo stuff >a &&
		but add a &&
		test_tick &&
		but cummit -m A &&
		but tag A &&

		but checkout -b B A &&
		but mv a b &&
		test_tick &&
		but cummit -m B &&

		but checkout -b C A &&
		but mv a c &&
		test_tick &&
		but cummit -m C
	)
}

test_expect_success 'merge has correct working tree contents' '
	test_setup_rename_rename_1to2 &&
	(
		cd rename-rename-1to2 &&

		but checkout C^0 &&

		test_must_fail but merge -s recursive B^0 &&

		but ls-files -s >out &&
		test_line_count = 3 out &&
		but ls-files -u >out &&
		test_line_count = 3 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_missing a &&
		but rev-parse >expect   \
			A:a   A:a   A:a \
			A:a   A:a       &&
		but rev-parse >actual    \
			:1:a  :3:b  :2:c &&
		but hash-object >>actual \
			b     c          &&
		test_cmp expect actual
	)
'

# Testcase setup for rename/rename(1to2)/add-source conflict:
#   cummit A: new file: a
#   cummit B: rename a->b
#   cummit C: rename a->c, add completely different a
#
# Merging of B & C should NOT be clean; there's a rename/rename conflict

test_setup_rename_rename_1to2_add_source_1 () {
	test_create_repo rename-rename-1to2-add-source-1 &&
	(
		cd rename-rename-1to2-add-source-1 &&

		printf "1\n2\n3\n4\n5\n6\n7\n" >a &&
		but add a &&
		but cummit -m A &&
		but tag A &&

		but checkout -b B A &&
		but mv a b &&
		but cummit -m B &&

		but checkout -b C A &&
		but mv a c &&
		echo something completely different >a &&
		but add a &&
		but cummit -m C
	)
}

test_expect_failure 'detect conflict with rename/rename(1to2)/add-source merge' '
	test_setup_rename_rename_1to2_add_source_1 &&
	(
		cd rename-rename-1to2-add-source-1 &&

		but checkout B^0 &&

		test_must_fail but merge -s recursive C^0 &&

		but ls-files -s >out &&
		test_line_count = 4 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		but rev-parse >expect         \
			C:a   A:a   B:b   C:C &&
		but rev-parse >actual          \
			:3:a  :1:a  :2:b  :3:c &&
		test_cmp expect actual &&

		test_path_is_file a &&
		test_path_is_file b &&
		test_path_is_file c
	)
'

test_setup_rename_rename_1to2_add_source_2 () {
	test_create_repo rename-rename-1to2-add-source-2 &&
	(
		cd rename-rename-1to2-add-source-2 &&

		>a &&
		but add a &&
		test_tick &&
		but cummit -m base &&
		but tag A &&

		but checkout -b B A &&
		but mv a b &&
		test_tick &&
		but cummit -m one &&

		but checkout -b C A &&
		but mv a b &&
		echo important-info >a &&
		but add a &&
		test_tick &&
		but cummit -m two
	)
}

test_expect_failure 'rename/rename/add-source still tracks new a file' '
	test_setup_rename_rename_1to2_add_source_2 &&
	(
		cd rename-rename-1to2-add-source-2 &&

		but checkout C^0 &&
		but merge -s recursive B^0 &&

		but ls-files -s >out &&
		test_line_count = 2 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		but rev-parse >expect \
			C:a   A:a     &&
		but rev-parse >actual \
			:0:a  :0:b    &&
		test_cmp expect actual
	)
'

test_setup_rename_rename_1to2_add_dest () {
	test_create_repo rename-rename-1to2-add-dest &&
	(
		cd rename-rename-1to2-add-dest &&

		echo stuff >a &&
		but add a &&
		test_tick &&
		but cummit -m base &&
		but tag A &&

		but checkout -b B A &&
		but mv a b &&
		echo precious-data >c &&
		but add c &&
		test_tick &&
		but cummit -m one &&

		but checkout -b C A &&
		but mv a c &&
		echo important-info >b &&
		but add b &&
		test_tick &&
		but cummit -m two
	)
}

test_expect_success 'rename/rename/add-dest merge still knows about conflicting file versions' '
	test_setup_rename_rename_1to2_add_dest &&
	(
		cd rename-rename-1to2-add-dest &&

		but checkout C^0 &&
		test_must_fail but merge -s recursive B^0 &&

		but ls-files -s >out &&
		test_line_count = 5 out &&
		but ls-files -u b >out &&
		test_line_count = 2 out &&
		but ls-files -u c >out &&
		test_line_count = 2 out &&
		but ls-files -o >out &&
		test_line_count = 1 out &&

		but rev-parse >expect               \
			A:a   C:b   B:b   C:c   B:c &&
		but rev-parse >actual                \
			:1:a  :2:b  :3:b  :2:c  :3:c &&
		test_cmp expect actual &&

		# Record some contents for re-doing merges
		but cat-file -p A:a >stuff &&
		but cat-file -p C:b >important_info &&
		but cat-file -p B:c >precious_data &&
		>empty &&

		# Test the merge in b
		test_must_fail but merge-file \
			-L "HEAD" \
			-L "" \
			-L "B^0" \
			important_info empty stuff &&
		test_cmp important_info b &&

		# Test the merge in c
		test_must_fail but merge-file \
			-L "HEAD" \
			-L "" \
			-L "B^0" \
			stuff empty precious_data &&
		test_cmp stuff c
	)
'

# Testcase rad, rename/add/delete
#   cummit O: foo
#   cummit A: rm foo, add different bar
#   cummit B: rename foo->bar
#   Expected: CONFLICT (rename/add/delete), two-way merged bar

test_setup_rad () {
	test_create_repo rad &&
	(
		cd rad &&
		echo "original file" >foo &&
		but add foo &&
		but cummit -m "original" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		but rm foo &&
		echo "different file" >bar &&
		but add bar &&
		but cummit -m "Remove foo, add bar" &&

		but checkout B &&
		but mv foo bar &&
		but cummit -m "rename foo to bar"
	)
}

test_expect_merge_algorithm failure success 'rad-check: rename/add/delete conflict' '
	test_setup_rad &&
	(
		cd rad &&

		but checkout B^0 &&
		test_must_fail but merge -s recursive A^0 >out 2>err &&

		# Instead of requiring the output to contain one combined line
		#   CONFLICT (rename/add/delete)
		# or perhaps two lines:
		#   CONFLICT (rename/add): new file collides with rename target
		#   CONFLICT (rename/delete): rename source removed on other side
		# and instead of requiring "rename/add" instead of "add/add",
		# be flexible in the type of console output message(s) reported
		# for this particular case; we will be more stringent about the
		# contents of the index and working directory.
		test_i18ngrep "CONFLICT (.*/add)" out &&
		test_i18ngrep "CONFLICT (rename.*/delete)" out &&
		test_must_be_empty err &&

		but ls-files -s >file_count &&
		test_line_count = 2 file_count &&
		but ls-files -u >file_count &&
		test_line_count = 2 file_count &&
		but ls-files -o >file_count &&
		test_line_count = 3 file_count &&

		but rev-parse >actual \
			:2:bar :3:bar &&
		but rev-parse >expect \
			B:bar  A:bar  &&

		test_path_is_missing foo &&
		# bar should have two-way merged contents of the different
		# versions of bar; check that content from both sides is
		# present.
		grep original bar &&
		grep different bar
	)
'

# Testcase rrdd, rename/rename(2to1)/delete/delete
#   cummit O: foo, bar
#   cummit A: rename foo->baz, rm bar
#   cummit B: rename bar->baz, rm foo
#   Expected: CONFLICT (rename/rename/delete/delete), two-way merged baz

test_setup_rrdd () {
	test_create_repo rrdd &&
	(
		cd rrdd &&
		echo foo >foo &&
		echo bar >bar &&
		but add foo bar &&
		but cummit -m O &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		but mv foo baz &&
		but rm bar &&
		but cummit -m "Rename foo, remove bar" &&

		but checkout B &&
		but mv bar baz &&
		but rm foo &&
		but cummit -m "Rename bar, remove foo"
	)
}

test_expect_merge_algorithm failure success 'rrdd-check: rename/rename(2to1)/delete/delete conflict' '
	test_setup_rrdd &&
	(
		cd rrdd &&

		but checkout A^0 &&
		test_must_fail but merge -s recursive B^0 >out 2>err &&

		# Instead of requiring the output to contain one combined line
		#   CONFLICT (rename/rename/delete/delete)
		# or perhaps two lines:
		#   CONFLICT (rename/rename): ...
		#   CONFLICT (rename/delete): info about pair 1
		#   CONFLICT (rename/delete): info about pair 2
		# and instead of requiring "rename/rename" instead of "add/add",
		# be flexible in the type of console output message(s) reported
		# for this particular case; we will be more stringent about the
		# contents of the index and working directory.
		test_i18ngrep "CONFLICT (\(.*\)/\1)" out &&
		test_i18ngrep "CONFLICT (rename.*delete)" out &&
		test_must_be_empty err &&

		but ls-files -s >file_count &&
		test_line_count = 2 file_count &&
		but ls-files -u >file_count &&
		test_line_count = 2 file_count &&
		but ls-files -o >file_count &&
		test_line_count = 3 file_count &&

		but rev-parse >actual \
			:2:baz :3:baz &&
		but rev-parse >expect \
			O:foo  O:bar  &&

		test_path_is_missing foo &&
		test_path_is_missing bar &&
		# baz should have two-way merged contents of the original
		# contents of foo and bar; check that content from both sides
		# is present.
		grep foo baz &&
		grep bar baz
	)
'

# Testcase mod6, chains of rename/rename(1to2) and rename/rename(2to1)
#   cummit O: one,      three,       five
#   cummit A: one->two, three->four, five->six
#   cummit B: one->six, three->two,  five->four
#   Expected: six CONFLICT(rename/rename) messages, each path in two of the
#             multi-way merged contents found in two, four, six

test_setup_mod6 () {
	test_create_repo mod6 &&
	(
		cd mod6 &&
		test_seq 11 19 >one &&
		test_seq 31 39 >three &&
		test_seq 51 59 >five &&
		but add . &&
		test_tick &&
		but cummit -m "O" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		test_seq 10 19 >one &&
		echo 40        >>three &&
		but add one three &&
		but mv  one   two  &&
		but mv  three four &&
		but mv  five  six  &&
		test_tick &&
		but cummit -m "A" &&

		but checkout B &&
		echo 20    >>one       &&
		echo forty >>three     &&
		echo 60    >>five      &&
		but add one three five &&
		but mv  one   six  &&
		but mv  three two  &&
		but mv  five  four &&
		test_tick &&
		but cummit -m "B"
	)
}

test_expect_merge_algorithm failure success 'mod6-check: chains of rename/rename(1to2) and rename/rename(2to1)' '
	test_setup_mod6 &&
	(
		cd mod6 &&

		but checkout A^0 &&

		test_must_fail but merge -s recursive B^0 >out 2>err &&

		test_i18ngrep "CONFLICT (rename/rename)" out &&
		test_must_be_empty err &&

		but ls-files -s >file_count &&
		test_line_count = 9 file_count &&
		but ls-files -u >file_count &&
		test_line_count = 9 file_count &&
		but ls-files -o >file_count &&
		test_line_count = 3 file_count &&

		test_seq 10 20 >merged-one &&
		test_seq 51 60 >merged-five &&
		# Determine what the merge of three would give us.
		test_seq 31 39 >three-base &&
		test_seq 31 40 >three-side-A &&
		test_seq 31 39 >three-side-B &&
		echo forty >>three-side-B &&
		test_must_fail but merge-file \
			-L "HEAD:four" \
			-L "" \
			-L "B^0:two" \
			three-side-A three-base three-side-B &&
		sed -e "s/^\([<=>]\)/\1\1/" three-side-A >merged-three &&

		# Verify the index is as expected
		but rev-parse >actual         \
			:2:two       :3:two   \
			:2:four      :3:four  \
			:2:six       :3:six   &&
		but hash-object >expect           \
			merged-one   merged-three \
			merged-three merged-five  \
			merged-five  merged-one   &&
		test_cmp expect actual &&

		but cat-file -p :2:two >expect &&
		but cat-file -p :3:two >other &&
		>empty &&
		test_must_fail but merge-file    \
			-L "HEAD"  -L ""  -L "B^0" \
			expect     empty  other &&
		test_cmp expect two &&

		but cat-file -p :2:four >expect &&
		but cat-file -p :3:four >other &&
		test_must_fail but merge-file    \
			-L "HEAD"  -L ""  -L "B^0" \
			expect     empty  other &&
		test_cmp expect four &&

		but cat-file -p :2:six >expect &&
		but cat-file -p :3:six >other &&
		test_must_fail but merge-file    \
			-L "HEAD"  -L ""  -L "B^0" \
			expect     empty  other &&
		test_cmp expect six
	)
'

test_conflicts_with_adds_and_renames() {
	sideL=$1
	sideR=$2

	# Setup:
	#          L
	#         / \
	#     main   ?
	#         \ /
	#          R
	#
	# Where:
	#   Both L and R have files named 'three' which collide.  Each of
	#   the colliding files could have been involved in a rename, in
	#   which case there was a file named 'one' or 'two' that was
	#   modified on the opposite side of history and renamed into the
	#   collision on this side of history.
	#
	# Questions:
	#   1) The index should contain both a stage 2 and stage 3 entry
	#      for the colliding file.  Does it?
	#   2) When renames are involved, the content merges are clean, so
	#      the index should reflect the content merges, not merely the
	#      version of the colliding file from the prior cummit.  Does
	#      it?
	#   3) There should be a file in the worktree named 'three'
	#      containing the two-way merged contents of the content-merged
	#      versions of 'three' from each of the two colliding
	#      files.  Is it present?
	#   4) There should not be any three~* files in the working
	#      tree
	test_setup_collision_conflict () {
	#test_expect_success "setup simple $sideL/$sideR conflict" '
		test_create_repo simple_${sideL}_${sideR} &&
		(
			cd simple_${sideL}_${sideR} &&

			# Create some related files now
			for i in $(test_seq 1 10)
			do
				echo Random base content line $i
			done >file_v1 &&
			cp file_v1 file_v2 &&
			echo modification >>file_v2 &&

			cp file_v1 file_v3 &&
			echo more stuff >>file_v3 &&
			cp file_v3 file_v4 &&
			echo yet more stuff >>file_v4 &&

			# Use a tag to record both these files for simple
			# access, and clean out these untracked files
			but tag file_v1 $(but hash-object -w file_v1) &&
			but tag file_v2 $(but hash-object -w file_v2) &&
			but tag file_v3 $(but hash-object -w file_v3) &&
			but tag file_v4 $(but hash-object -w file_v4) &&
			but clean -f &&

			# Setup original cummit (or merge-base), consisting of
			# files named "one" and "two" if renames were involved.
			touch irrelevant_file &&
			but add irrelevant_file &&
			if [ $sideL = "rename" ]
			then
				but show file_v1 >one &&
				but add one
			fi &&
			if [ $sideR = "rename" ]
			then
				but show file_v3 >two &&
				but add two
			fi &&
			test_tick && but cummit -m initial &&

			but branch L &&
			but branch R &&

			# Handle the left side
			but checkout L &&
			if [ $sideL = "rename" ]
			then
				but mv one three
			else
				but show file_v2 >three &&
				but add three
			fi &&
			if [ $sideR = "rename" ]
			then
				but show file_v4 >two &&
				but add two
			fi &&
			test_tick && but cummit -m L &&

			# Handle the right side
			but checkout R &&
			if [ $sideL = "rename" ]
			then
				but show file_v2 >one &&
				but add one
			fi &&
			if [ $sideR = "rename" ]
			then
				but mv two three
			else
				but show file_v4 >three &&
				but add three
			fi &&
			test_tick && but cummit -m R
		)
	#'
	}

	test_expect_success "check simple $sideL/$sideR conflict" '
		test_setup_collision_conflict &&
		(
			cd simple_${sideL}_${sideR} &&

			but checkout L^0 &&

			# Merge must fail; there is a conflict
			test_must_fail but merge -s recursive R^0 &&

			# Make sure the index has the right number of entries
			but ls-files -s >out &&
			test_line_count = 3 out &&
			but ls-files -u >out &&
			test_line_count = 2 out &&
			# Ensure we have the correct number of untracked files
			but ls-files -o >out &&
			test_line_count = 1 out &&

			# Nothing should have touched irrelevant_file
			but rev-parse >actual      \
				:0:irrelevant_file \
				:2:three           \
				:3:three           &&
			but rev-parse >expected        \
				main:irrelevant_file \
				file_v2                \
				file_v4                &&
			test_cmp expected actual &&

			# Make sure we have the correct merged contents for
			# three
			but show file_v1 >expected &&
			cat <<-\EOF >>expected &&
			<<<<<<< HEAD
			modification
			=======
			more stuff
			yet more stuff
			>>>>>>> R^0
			EOF

			test_cmp expected three
		)
	'
}

test_conflicts_with_adds_and_renames rename rename
test_conflicts_with_adds_and_renames rename add
test_conflicts_with_adds_and_renames add    rename
test_conflicts_with_adds_and_renames add    add

# Setup:
#          L
#         / \
#     main   ?
#         \ /
#          R
#
# Where:
#   main has two files, named 'one' and 'two'.
#   branches L and R both modify 'one', in conflicting ways.
#   branches L and R both modify 'two', in conflicting ways.
#   branch L also renames 'one' to 'three'.
#   branch R also renames 'two' to 'three'.
#
#   So, we have four different conflicting files that all end up at path
#   'three'.
test_setup_nested_conflicts_from_rename_rename () {
	test_create_repo nested_conflicts_from_rename_rename &&
	(
		cd nested_conflicts_from_rename_rename &&

		# Create some related files now
		for i in $(test_seq 1 10)
		do
			echo Random base content line $i
		done >file_v1 &&

		cp file_v1 file_v2 &&
		cp file_v1 file_v3 &&
		cp file_v1 file_v4 &&
		cp file_v1 file_v5 &&
		cp file_v1 file_v6 &&

		echo one  >>file_v1 &&
		echo uno  >>file_v2 &&
		echo eins >>file_v3 &&

		echo two  >>file_v4 &&
		echo dos  >>file_v5 &&
		echo zwei >>file_v6 &&

		# Setup original cummit (or merge-base), consisting of
		# files named "one" and "two".
		mv file_v1 one &&
		mv file_v4 two &&
		but add one two &&
		test_tick && but cummit -m english &&

		but branch L &&
		but branch R &&

		# Handle the left side
		but checkout L &&
		but rm one two &&
		mv -f file_v2 three &&
		mv -f file_v5 two &&
		but add two three &&
		test_tick && but cummit -m spanish &&

		# Handle the right side
		but checkout R &&
		but rm one two &&
		mv -f file_v3 one &&
		mv -f file_v6 three &&
		but add one three &&
		test_tick && but cummit -m german
	)
}

test_expect_success 'check nested conflicts from rename/rename(2to1)' '
	test_setup_nested_conflicts_from_rename_rename &&
	(
		cd nested_conflicts_from_rename_rename &&

		but checkout L^0 &&

		# Merge must fail; there is a conflict
		test_must_fail but merge -s recursive R^0 &&

		# Make sure the index has the right number of entries
		but ls-files -s >out &&
		test_line_count = 2 out &&
		but ls-files -u >out &&
		test_line_count = 2 out &&
		# Ensure we have the correct number of untracked files
		but ls-files -o >out &&
		test_line_count = 1 out &&

		# Compare :2:three to expected values
		but cat-file -p main:one >base &&
		but cat-file -p L:three >ours &&
		but cat-file -p R:one >theirs &&
		test_must_fail but merge-file    \
			-L "HEAD:three"  -L ""  -L "R^0:one" \
			ours             base   theirs &&
		sed -e "s/^\([<=>]\)/\1\1/" ours >L-three &&
		but cat-file -p :2:three >expect &&
		test_cmp expect L-three &&

		# Compare :2:three to expected values
		but cat-file -p main:two >base &&
		but cat-file -p L:two >ours &&
		but cat-file -p R:three >theirs &&
		test_must_fail but merge-file    \
			-L "HEAD:two"  -L ""  -L "R^0:three" \
			ours           base   theirs &&
		sed -e "s/^\([<=>]\)/\1\1/" ours >R-three &&
		but cat-file -p :3:three >expect &&
		test_cmp expect R-three &&

		# Compare three to expected contents
		>empty &&
		test_must_fail but merge-file    \
			-L "HEAD"  -L ""  -L "R^0" \
			L-three    empty  R-three &&
		test_cmp three L-three
	)
'

# Testcase rename/rename(1to2) of a binary file
#   cummit O: orig
#   cummit A: orig-A
#   cummit B: orig-B
#   Expected: CONFLICT(rename/rename) message, three unstaged entries in the
#             index, and contents of orig-[AB] at path orig-[AB]
test_setup_rename_rename_1_to_2_binary () {
	test_create_repo rename_rename_1_to_2_binary &&
	(
		cd rename_rename_1_to_2_binary &&

		echo '* binary' >.butattributes &&
		but add .butattributes &&

		test_seq 1 10 >orig &&
		but add orig &&
		but cummit -m orig &&

		but branch A &&
		but branch B &&

		but checkout A &&
		but mv orig orig-A &&
		test_seq 1 11 >orig-A &&
		but add orig-A &&
		but cummit -m orig-A &&

		but checkout B &&
		but mv orig orig-B &&
		test_seq 0 10 >orig-B &&
		but add orig-B &&
		but cummit -m orig-B

	)
}

test_expect_success 'rename/rename(1to2) with a binary file' '
	test_setup_rename_rename_1_to_2_binary &&
	(
		cd rename_rename_1_to_2_binary &&

		but checkout A^0 &&

		test_must_fail but merge -s recursive B^0 &&

		# Make sure the index has the right number of entries
		but ls-files -s >actual &&
		test_line_count = 4 actual &&

		but rev-parse A:orig-A B:orig-B >expect &&
		but hash-object orig-A orig-B >actual &&
		test_cmp expect actual
	)
'

test_done
