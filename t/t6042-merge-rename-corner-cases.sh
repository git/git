#!/bin/sh

test_description="recursive merge corner cases w/ renames but not criss-crosses"
# t6036 has corner cases that involve both criss-cross merges and renames

. ./test-lib.sh

test_setup_rename_delete_untracked () {
	test_create_repo rename-delete-untracked &&
	(
		cd rename-delete-untracked &&

		echo "A pretty inscription" >ring &&
		git add ring &&
		test_tick &&
		git commit -m beginning &&

		git branch people &&
		git checkout -b rename-the-ring &&
		git mv ring one-ring-to-rule-them-all &&
		test_tick &&
		git commit -m fullname &&

		git checkout people &&
		git rm ring &&
		echo gollum >owner &&
		git add owner &&
		test_tick &&
		git commit -m track-people-instead-of-objects &&
		echo "Myyy PRECIOUSSS" >ring
	)
}

test_expect_success "Does git preserve Gollum's precious artifact?" '
	test_setup_rename_delete_untracked &&
	(
		cd rename-delete-untracked &&

		test_must_fail git merge -s recursive rename-the-ring &&

		# Make sure git did not delete an untracked file
		test_path_is_file ring
	)
'

# Testcase setup for rename/modify/add-source:
#   Commit A: new file: a
#   Commit B: modify a slightly
#   Commit C: rename a->b, add completely different a
#
# We should be able to merge B & C cleanly

test_setup_rename_modify_add_source () {
	test_create_repo rename-modify-add-source &&
	(
		cd rename-modify-add-source &&

		printf "1\n2\n3\n4\n5\n6\n7\n" >a &&
		git add a &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		echo 8 >>a &&
		git add a &&
		git commit -m B &&

		git checkout -b C A &&
		git mv a b &&
		echo something completely different >a &&
		git add a &&
		git commit -m C
	)
}

test_expect_failure 'rename/modify/add-source conflict resolvable' '
	test_setup_rename_modify_add_source &&
	(
		cd rename-modify-add-source &&

		git checkout B^0 &&

		git merge -s recursive C^0 &&

		git rev-parse >expect \
			B:a   C:a     &&
		git rev-parse >actual \
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
		git add a b &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git mv a c &&
		echo "Completely different content" >a &&
		git add a &&
		git commit -m B &&

		git checkout -b C A &&
		echo 6 >>a &&
		git add a &&
		git commit -m C
	)
}

test_expect_failure 'conflict caused if rename not detected' '
	test_setup_break_detection_1 &&
	(
		cd break-detection-1 &&

		git checkout -q C^0 &&
		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		test_line_count = 6 c &&
		git rev-parse >expect \
			B:a   A:b     &&
		git rev-parse >actual \
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
		git add a b &&
		git commit -m A &&
		git tag A &&

		git checkout -b D A &&
		echo 7 >>a &&
		git add a &&
		git mv a c &&
		echo "Completely different content" >a &&
		git add a &&
		git commit -m D &&

		git checkout -b E A &&
		git rm a &&
		echo "Completely different content" >>a &&
		git add a &&
		git commit -m E
	)
}

test_expect_failure 'missed conflict if rename not detected' '
	test_setup_break_detection_2 &&
	(
		cd break-detection-2 &&

		git checkout -q E^0 &&
		test_must_fail git merge -s recursive D^0
	)
'

# Tests for undetected rename/add-source causing a file to erroneously be
# deleted (and for mishandled rename/rename(1to1) causing the same issue).
#
# This test uses a rename/rename(1to1)+add-source conflict (1to1 means the
# same file is renamed on both sides to the same thing; it should trigger
# the 1to2 logic, which it would do if the add-source didn't cause issues
# for git's rename detection):
#   Commit A: new file: a
#   Commit B: rename a->b
#   Commit C: rename a->b, add unrelated a

test_setup_break_detection_3 () {
	test_create_repo break-detection-3 &&
	(
		cd break-detection-3 &&

		printf "1\n2\n3\n4\n5\n" >a &&
		git add a &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git mv a b &&
		git commit -m B &&

		git checkout -b C A &&
		git mv a b &&
		echo foobar >a &&
		git add a &&
		git commit -m C
	)
}

test_expect_failure 'detect rename/add-source and preserve all data' '
	test_setup_break_detection_3 &&
	(
		cd break-detection-3 &&

		git checkout B^0 &&

		git merge -s recursive C^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_file a &&
		test_path_is_file b &&

		git rev-parse >expect \
			A:a   C:a     &&
		git rev-parse >actual \
			:0:b  :0:a    &&
		test_cmp expect actual
	)
'

test_expect_failure 'detect rename/add-source and preserve all data, merge other way' '
	test_setup_break_detection_3 &&
	(
		cd break-detection-3 &&

		git checkout C^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_file a &&
		test_path_is_file b &&

		git rev-parse >expect \
			A:a   C:a     &&
		git rev-parse >actual \
			:0:b  :0:a    &&
		test_cmp expect actual
	)
'

test_setup_rename_directory () {
	test_create_repo rename-directory-$1 &&
	(
		cd rename-directory-$1 &&

		printf "1\n2\n3\n4\n5\n6\n" >file &&
		git add file &&
		test_tick &&
		git commit -m base &&
		git tag base &&

		git checkout -b right &&
		echo 7 >>file &&
		mkdir newfile &&
		echo junk >newfile/realfile &&
		git add file newfile/realfile &&
		test_tick &&
		git commit -m right &&

		git checkout -b left-conflict base &&
		echo 8 >>file &&
		git add file &&
		git mv file newfile &&
		test_tick &&
		git commit -m left &&

		git checkout -b left-clean base &&
		echo 0 >newfile &&
		cat file >>newfile &&
		git add newfile &&
		git rm file &&
		test_tick &&
		git commit -m left
	)
}

test_expect_success 'rename/directory conflict + clean content merge' '
	test_setup_rename_directory 1a &&
	(
		cd rename-directory-1a &&

		git checkout left-clean^0 &&

		test_must_fail git merge -s recursive right^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 2 out &&

		echo 0 >expect &&
		git cat-file -p base:file >>expect &&
		echo 7 >>expect &&
		test_cmp expect newfile~HEAD &&

		test $(git rev-parse :2:newfile) = $(git hash-object expect) &&

		test_path_is_file newfile/realfile &&
		test_path_is_file newfile~HEAD
	)
'

test_expect_success 'rename/directory conflict + content merge conflict' '
	test_setup_rename_directory 1b &&
	(
		cd rename-directory-1b &&

		git reset --hard &&
		git clean -fdqx &&

		git checkout left-conflict^0 &&

		test_must_fail git merge -s recursive right^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 2 out &&

		git cat-file -p left-conflict:newfile >left &&
		git cat-file -p base:file    >base &&
		git cat-file -p right:file   >right &&
		test_must_fail git merge-file \
			-L "HEAD:newfile" \
			-L "" \
			-L "right^0:file" \
			left base right &&
		test_cmp left newfile~HEAD &&

		git rev-parse >expect                                 \
			base:file   left-conflict:newfile  right:file &&
		git rev-parse >actual                                 \
			:1:newfile  :2:newfile             :3:newfile &&
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
		git add sub/file &&
		test_tick &&
		git commit -m base &&
		git tag base &&

		git checkout -b right &&
		echo 7 >>sub/file &&
		git add sub/file &&
		test_tick &&
		git commit -m right &&

		git checkout -b left base &&
		echo 0 >newfile &&
		cat sub/file >>newfile &&
		git rm sub/file &&
		mv newfile sub &&
		git add sub &&
		test_tick &&
		git commit -m left
	)
}

test_expect_success 'disappearing dir in rename/directory conflict handled' '
	test_setup_rename_directory_2 &&
	(
		cd rename-directory-2 &&

		git checkout left^0 &&

		git merge -s recursive right^0 &&

		git ls-files -s >out &&
		test_line_count = 1 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		echo 0 >expect &&
		git cat-file -p base:sub/file >>expect &&
		echo 7 >>expect &&
		test_cmp expect sub &&

		test_path_is_file sub
	)
'

# Test for basic rename/add-dest conflict, with rename needing content merge:
#   Commit O: a
#   Commit A: rename a->b, modifying b too
#   Commit B: modify a, add different b

test_setup_rename_with_content_merge_and_add () {
	test_create_repo rename-with-content-merge-and-add-$1 &&
	(
		cd rename-with-content-merge-and-add-$1 &&

		test_seq 1 5 >a &&
		git add a &&
		git commit -m O &&
		git tag O &&

		git checkout -b A O &&
		git mv a b &&
		test_seq 0 5 >b &&
		git add b &&
		git commit -m A &&

		git checkout -b B O &&
		echo 6 >>a &&
		echo hello world >b &&
		git add a b &&
		git commit -m B
	)
}

test_expect_success 'handle rename-with-content-merge vs. add' '
	test_setup_rename_with_content_merge_and_add AB &&
	(
		cd rename-with-content-merge-and-add-AB &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (rename/add)" out &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		# Also, make sure both unmerged entries are for "b"
		git ls-files -u b >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_missing a &&
		test_path_is_file b &&

		test_seq 0 6 >tmp &&
		git hash-object tmp >expect &&
		git rev-parse B:b >>expect &&
		git rev-parse >actual  \
			:2:b    :3:b   &&
		test_cmp expect actual &&

		# Test that the two-way merge in b is as expected
		git cat-file -p :2:b >>ours &&
		git cat-file -p :3:b >>theirs &&
		>empty &&
		test_must_fail git merge-file \
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

		git reset --hard &&
		git clean -fdx &&

		git checkout B^0 &&

		test_must_fail git merge -s recursive A^0 >out &&
		test_i18ngrep "CONFLICT (rename/add)" out &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		# Also, make sure both unmerged entries are for "b"
		git ls-files -u b >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_missing a &&
		test_path_is_file b &&

		test_seq 0 6 >tmp &&
		git rev-parse B:b >expect &&
		git hash-object tmp >>expect &&
		git rev-parse >actual  \
			:2:b    :3:b   &&
		test_cmp expect actual &&

		# Test that the two-way merge in b is as expected
		git cat-file -p :2:b >>ours &&
		git cat-file -p :3:b >>theirs &&
		>empty &&
		test_must_fail git merge-file \
			-L "HEAD" \
			-L "" \
			-L "A^0" \
			ours empty theirs &&
		test_cmp ours b
	)
'

# Test for all kinds of things that can go wrong with rename/rename (2to1):
#   Commit A: new files: a & b
#   Commit B: rename a->c, modify b
#   Commit C: rename b->c, modify a
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
		git add a b &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git mv a c &&
		echo 0 >>b &&
		git add b &&
		git commit -m B &&

		git checkout -b C A &&
		git mv b c &&
		echo 6 >>a &&
		git add a &&
		git commit -m C
	)
}

test_expect_success 'handle rename/rename (2to1) conflict correctly' '
	test_setup_rename_rename_2to1 &&
	(
		cd rename-rename-2to1 &&

		git checkout B^0 &&

		test_must_fail git merge -s recursive C^0 >out &&
		test_i18ngrep "CONFLICT (rename/rename)" out &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -u c >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_missing a &&
		test_path_is_missing b &&

		git rev-parse >expect  \
			C:a     B:b    &&
		git rev-parse >actual  \
			:2:c    :3:c   &&
		test_cmp expect actual &&

		# Test that the two-way merge in new_a is as expected
		git cat-file -p :2:c >>ours &&
		git cat-file -p :3:c >>theirs &&
		>empty &&
		test_must_fail git merge-file \
			-L "HEAD" \
			-L "" \
			-L "C^0" \
			ours empty theirs &&
		git hash-object c >actual &&
		git hash-object ours >expect &&
		test_cmp expect actual
	)
'

# Testcase setup for simple rename/rename (1to2) conflict:
#   Commit A: new file: a
#   Commit B: rename a->b
#   Commit C: rename a->c
test_setup_rename_rename_1to2 () {
	test_create_repo rename-rename-1to2 &&
	(
		cd rename-rename-1to2 &&

		echo stuff >a &&
		git add a &&
		test_tick &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git mv a b &&
		test_tick &&
		git commit -m B &&

		git checkout -b C A &&
		git mv a c &&
		test_tick &&
		git commit -m C
	)
}

test_expect_success 'merge has correct working tree contents' '
	test_setup_rename_rename_1to2 &&
	(
		cd rename-rename-1to2 &&

		git checkout C^0 &&

		test_must_fail git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		test_path_is_missing a &&
		git rev-parse >expect   \
			A:a   A:a   A:a \
			A:a   A:a       &&
		git rev-parse >actual    \
			:1:a  :3:b  :2:c &&
		git hash-object >>actual \
			b     c          &&
		test_cmp expect actual
	)
'

# Testcase setup for rename/rename(1to2)/add-source conflict:
#   Commit A: new file: a
#   Commit B: rename a->b
#   Commit C: rename a->c, add completely different a
#
# Merging of B & C should NOT be clean; there's a rename/rename conflict

test_setup_rename_rename_1to2_add_source_1 () {
	test_create_repo rename-rename-1to2-add-source-1 &&
	(
		cd rename-rename-1to2-add-source-1 &&

		printf "1\n2\n3\n4\n5\n6\n7\n" >a &&
		git add a &&
		git commit -m A &&
		git tag A &&

		git checkout -b B A &&
		git mv a b &&
		git commit -m B &&

		git checkout -b C A &&
		git mv a c &&
		echo something completely different >a &&
		git add a &&
		git commit -m C
	)
}

test_expect_failure 'detect conflict with rename/rename(1to2)/add-source merge' '
	test_setup_rename_rename_1to2_add_source_1 &&
	(
		cd rename-rename-1to2-add-source-1 &&

		git checkout B^0 &&

		test_must_fail git merge -s recursive C^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >expect         \
			C:a   A:a   B:b   C:C &&
		git rev-parse >actual          \
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
		git add a &&
		test_tick &&
		git commit -m base &&
		git tag A &&

		git checkout -b B A &&
		git mv a b &&
		test_tick &&
		git commit -m one &&

		git checkout -b C A &&
		git mv a b &&
		echo important-info >a &&
		git add a &&
		test_tick &&
		git commit -m two
	)
}

test_expect_failure 'rename/rename/add-source still tracks new a file' '
	test_setup_rename_rename_1to2_add_source_2 &&
	(
		cd rename-rename-1to2-add-source-2 &&

		git checkout C^0 &&
		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >expect \
			C:a   A:a     &&
		git rev-parse >actual \
			:0:a  :0:b    &&
		test_cmp expect actual
	)
'

test_setup_rename_rename_1to2_add_dest () {
	test_create_repo rename-rename-1to2-add-dest &&
	(
		cd rename-rename-1to2-add-dest &&

		echo stuff >a &&
		git add a &&
		test_tick &&
		git commit -m base &&
		git tag A &&

		git checkout -b B A &&
		git mv a b &&
		echo precious-data >c &&
		git add c &&
		test_tick &&
		git commit -m one &&

		git checkout -b C A &&
		git mv a c &&
		echo important-info >b &&
		git add b &&
		test_tick &&
		git commit -m two
	)
}

test_expect_success 'rename/rename/add-dest merge still knows about conflicting file versions' '
	test_setup_rename_rename_1to2_add_dest &&
	(
		cd rename-rename-1to2-add-dest &&

		git checkout C^0 &&
		test_must_fail git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u b >out &&
		test_line_count = 2 out &&
		git ls-files -u c >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >expect               \
			A:a   C:b   B:b   C:c   B:c &&
		git rev-parse >actual                \
			:1:a  :2:b  :3:b  :2:c  :3:c &&
		test_cmp expect actual &&

		# Record some contents for re-doing merges
		git cat-file -p A:a >stuff &&
		git cat-file -p C:b >important_info &&
		git cat-file -p B:c >precious_data &&
		>empty &&

		# Test the merge in b
		test_must_fail git merge-file \
			-L "HEAD" \
			-L "" \
			-L "B^0" \
			important_info empty stuff &&
		test_cmp important_info b &&

		# Test the merge in c
		test_must_fail git merge-file \
			-L "HEAD" \
			-L "" \
			-L "B^0" \
			stuff empty precious_data &&
		test_cmp stuff c
	)
'

# Testcase rad, rename/add/delete
#   Commit O: foo
#   Commit A: rm foo, add different bar
#   Commit B: rename foo->bar
#   Expected: CONFLICT (rename/add/delete), two-way merged bar

test_setup_rad () {
	test_create_repo rad &&
	(
		cd rad &&
		echo "original file" >foo &&
		git add foo &&
		git commit -m "original" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git rm foo &&
		echo "different file" >bar &&
		git add bar &&
		git commit -m "Remove foo, add bar" &&

		git checkout B &&
		git mv foo bar &&
		git commit -m "rename foo to bar"
	)
}

test_expect_failure 'rad-check: rename/add/delete conflict' '
	test_setup_rad &&
	(
		cd rad &&

		git checkout B^0 &&
		test_must_fail git merge -s recursive A^0 >out 2>err &&

		# Not sure whether the output should contain just one
		# "CONFLICT (rename/add/delete)" line, or if it should break
		# it into a pair of "CONFLICT (rename/delete)" and
		# "CONFLICT (rename/add)"; allow for either.
		test_i18ngrep "CONFLICT (rename.*add)" out &&
		test_i18ngrep "CONFLICT (rename.*delete)" out &&
		test_must_be_empty err &&

		git ls-files -s >file_count &&
		test_line_count = 2 file_count &&
		git ls-files -u >file_count &&
		test_line_count = 2 file_count &&
		git ls-files -o >file_count &&
		test_line_count = 2 file_count &&

		git rev-parse >actual \
			:2:bar :3:bar &&
		git rev-parse >expect \
			B:bar  A:bar  &&

		test_cmp file_is_missing foo &&
		# bar should have two-way merged contents of the different
		# versions of bar; check that content from both sides is
		# present.
		grep original bar &&
		grep different bar
	)
'

# Testcase rrdd, rename/rename(2to1)/delete/delete
#   Commit O: foo, bar
#   Commit A: rename foo->baz, rm bar
#   Commit B: rename bar->baz, rm foo
#   Expected: CONFLICT (rename/rename/delete/delete), two-way merged baz

test_setup_rrdd () {
	test_create_repo rrdd &&
	(
		cd rrdd &&
		echo foo >foo &&
		echo bar >bar &&
		git add foo bar &&
		git commit -m O &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv foo baz &&
		git rm bar &&
		git commit -m "Rename foo, remove bar" &&

		git checkout B &&
		git mv bar baz &&
		git rm foo &&
		git commit -m "Rename bar, remove foo"
	)
}

test_expect_failure 'rrdd-check: rename/rename(2to1)/delete/delete conflict' '
	test_setup_rrdd &&
	(
		cd rrdd &&

		git checkout A^0 &&
		test_must_fail git merge -s recursive B^0 >out 2>err &&

		# Not sure whether the output should contain just one
		# "CONFLICT (rename/rename/delete/delete)" line, or if it
		# should break it into three: "CONFLICT (rename/rename)" and
		# two "CONFLICT (rename/delete)" lines; allow for either.
		test_i18ngrep "CONFLICT (rename/rename)" out &&
		test_i18ngrep "CONFLICT (rename.*delete)" out &&
		test_must_be_empty err &&

		git ls-files -s >file_count &&
		test_line_count = 2 file_count &&
		git ls-files -u >file_count &&
		test_line_count = 2 file_count &&
		git ls-files -o >file_count &&
		test_line_count = 2 file_count &&

		git rev-parse >actual \
			:2:baz :3:baz &&
		git rev-parse >expect \
			O:foo  O:bar  &&

		test_cmp file_is_missing foo &&
		test_cmp file_is_missing bar &&
		# baz should have two-way merged contents of the original
		# contents of foo and bar; check that content from both sides
		# is present.
		grep foo baz &&
		grep bar baz
	)
'

# Testcase mod6, chains of rename/rename(1to2) and rename/rename(2to1)
#   Commit O: one,      three,       five
#   Commit A: one->two, three->four, five->six
#   Commit B: one->six, three->two,  five->four
#   Expected: six CONFLICT(rename/rename) messages, each path in two of the
#             multi-way merged contents found in two, four, six

test_setup_mod6 () {
	test_create_repo mod6 &&
	(
		cd mod6 &&
		test_seq 11 19 >one &&
		test_seq 31 39 >three &&
		test_seq 51 59 >five &&
		git add . &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_seq 10 19 >one &&
		echo 40        >>three &&
		git add one three &&
		git mv  one   two  &&
		git mv  three four &&
		git mv  five  six  &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo 20    >>one       &&
		echo forty >>three     &&
		echo 60    >>five      &&
		git add one three five &&
		git mv  one   six  &&
		git mv  three two  &&
		git mv  five  four &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_failure 'mod6-check: chains of rename/rename(1to2) and rename/rename(2to1)' '
	test_setup_mod6 &&
	(
		cd mod6 &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&

		test_i18ngrep "CONFLICT (rename/rename)" out &&
		test_must_be_empty err &&

		git ls-files -s >file_count &&
		test_line_count = 6 file_count &&
		git ls-files -u >file_count &&
		test_line_count = 6 file_count &&
		git ls-files -o >file_count &&
		test_line_count = 3 file_count &&

		test_seq 10 20 >merged-one &&
		test_seq 51 60 >merged-five &&
		# Determine what the merge of three would give us.
		test_seq 30 40 >three-side-A &&
		test_seq 31 39 >three-side-B &&
		echo forty >three-side-B &&
		>empty &&
		test_must_fail git merge-file \
			-L "HEAD" \
			-L "" \
			-L "B^0" \
			three-side-A empty three-side-B &&
		sed -e "s/^\([<=>]\)/\1\1\1/" three-side-A >merged-three &&

		# Verify the index is as expected
		git rev-parse >actual         \
			:2:two       :3:two   \
			:2:four      :3:four  \
			:2:six       :3:six   &&
		git hash-object >expect           \
			merged-one   merged-three \
			merged-three merged-five  \
			merged-five  merged-one   &&
		test_cmp expect actual &&

		git cat-file -p :2:two >expect &&
		git cat-file -p :3:two >other &&
		test_must_fail git merge-file    \
			-L "HEAD"  -L ""  -L "B^0" \
			expect     empty  other &&
		test_cmp expect two &&

		git cat-file -p :2:four >expect &&
		git cat-file -p :3:four >other &&
		test_must_fail git merge-file    \
			-L "HEAD"  -L ""  -L "B^0" \
			expect     empty  other &&
		test_cmp expect four &&

		git cat-file -p :2:six >expect &&
		git cat-file -p :3:six >other &&
		test_must_fail git merge-file    \
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
	#   master   ?
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
	#      version of the colliding file from the prior commit.  Does
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
			git tag file_v1 $(git hash-object -w file_v1) &&
			git tag file_v2 $(git hash-object -w file_v2) &&
			git tag file_v3 $(git hash-object -w file_v3) &&
			git tag file_v4 $(git hash-object -w file_v4) &&
			git clean -f &&

			# Setup original commit (or merge-base), consisting of
			# files named "one" and "two" if renames were involved.
			touch irrelevant_file &&
			git add irrelevant_file &&
			if [ $sideL = "rename" ]
			then
				git show file_v1 >one &&
				git add one
			fi &&
			if [ $sideR = "rename" ]
			then
				git show file_v3 >two &&
				git add two
			fi &&
			test_tick && git commit -m initial &&

			git branch L &&
			git branch R &&

			# Handle the left side
			git checkout L &&
			if [ $sideL = "rename" ]
			then
				git mv one three
			else
				git show file_v2 >three &&
				git add three
			fi &&
			if [ $sideR = "rename" ]
			then
				git show file_v4 >two &&
				git add two
			fi &&
			test_tick && git commit -m L &&

			# Handle the right side
			git checkout R &&
			if [ $sideL = "rename" ]
			then
				git show file_v2 >one &&
				git add one
			fi &&
			if [ $sideR = "rename" ]
			then
				git mv two three
			else
				git show file_v4 >three &&
				git add three
			fi &&
			test_tick && git commit -m R
		)
	#'
	}

	test_expect_success "check simple $sideL/$sideR conflict" '
		test_setup_collision_conflict &&
		(
			cd simple_${sideL}_${sideR} &&

			git checkout L^0 &&

			# Merge must fail; there is a conflict
			test_must_fail git merge -s recursive R^0 &&

			# Make sure the index has the right number of entries
			git ls-files -s >out &&
			test_line_count = 3 out &&
			git ls-files -u >out &&
			test_line_count = 2 out &&
			# Ensure we have the correct number of untracked files
			git ls-files -o >out &&
			test_line_count = 1 out &&

			# Nothing should have touched irrelevant_file
			git rev-parse >actual      \
				:0:irrelevant_file \
				:2:three           \
				:3:three           &&
			git rev-parse >expected        \
				master:irrelevant_file \
				file_v2                \
				file_v4                &&
			test_cmp expected actual &&

			# Make sure we have the correct merged contents for
			# three
			git show file_v1 >expected &&
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
#   master   ?
#         \ /
#          R
#
# Where:
#   master has two files, named 'one' and 'two'.
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

		# Setup original commit (or merge-base), consisting of
		# files named "one" and "two".
		mv file_v1 one &&
		mv file_v4 two &&
		git add one two &&
		test_tick && git commit -m english &&

		git branch L &&
		git branch R &&

		# Handle the left side
		git checkout L &&
		git rm one two &&
		mv -f file_v2 three &&
		mv -f file_v5 two &&
		git add two three &&
		test_tick && git commit -m spanish &&

		# Handle the right side
		git checkout R &&
		git rm one two &&
		mv -f file_v3 one &&
		mv -f file_v6 three &&
		git add one three &&
		test_tick && git commit -m german
	)
}

test_expect_success 'check nested conflicts from rename/rename(2to1)' '
	test_setup_nested_conflicts_from_rename_rename &&
	(
		cd nested_conflicts_from_rename_rename &&

		git checkout L^0 &&

		# Merge must fail; there is a conflict
		test_must_fail git merge -s recursive R^0 &&

		# Make sure the index has the right number of entries
		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		# Ensure we have the correct number of untracked files
		git ls-files -o >out &&
		test_line_count = 1 out &&

		# Compare :2:three to expected values
		git cat-file -p master:one >base &&
		git cat-file -p L:three >ours &&
		git cat-file -p R:one >theirs &&
		test_must_fail git merge-file    \
			-L "HEAD:three"  -L ""  -L "R^0:one" \
			ours             base   theirs &&
		sed -e "s/^\([<=>]\)/\1\1/" ours >L-three &&
		git cat-file -p :2:three >expect &&
		test_cmp expect L-three &&

		# Compare :2:three to expected values
		git cat-file -p master:two >base &&
		git cat-file -p L:two >ours &&
		git cat-file -p R:three >theirs &&
		test_must_fail git merge-file    \
			-L "HEAD:two"  -L ""  -L "R^0:three" \
			ours           base   theirs &&
		sed -e "s/^\([<=>]\)/\1\1/" ours >R-three &&
		git cat-file -p :3:three >expect &&
		test_cmp expect R-three &&

		# Compare three to expected contents
		>empty &&
		test_must_fail git merge-file    \
			-L "HEAD"  -L ""  -L "R^0" \
			L-three    empty  R-three &&
		test_cmp three L-three
	)
'

test_done
