#!/bin/sh

test_description="recursive merge corner cases w/ renames but not criss-crosses"
# t6036 has corner cases that involve both criss-cross merges and renames

. ./test-lib.sh

test_expect_success 'setup rename/delete + untracked file' '
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
'

test_expect_success "Does git preserve Gollum's precious artifact?" '
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

test_expect_success 'setup rename/modify/add-source conflict' '
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
'

test_expect_failure 'rename/modify/add-source conflict resolvable' '
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

test_expect_success 'setup resolvable conflict missed if rename missed' '
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
'

test_expect_failure 'conflict caused if rename not detected' '
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

test_expect_success 'setup conflict resolved wrong if rename missed' '
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
'

test_expect_failure 'missed conflict if rename not detected' '
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

test_expect_success 'setup undetected rename/add-source causes data loss' '
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
'

test_expect_failure 'detect rename/add-source and preserve all data' '
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

test_expect_success 'setup content merge + rename/directory conflict' '
	test_create_repo rename-directory-1 &&
	(
		cd rename-directory-1 &&

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
'

test_expect_success 'rename/directory conflict + clean content merge' '
	(
		cd rename-directory-1 &&

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
	(
		cd rename-directory-1 &&

		git reset --hard &&
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
		test_cmp expect actual

		test_path_is_file newfile/realfile &&
		test_path_is_file newfile~HEAD
	)
'

test_expect_success 'setup content merge + rename/directory conflict w/ disappearing dir' '
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
'

test_expect_success 'disappearing dir in rename/directory conflict handled' '
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

test_expect_success 'setup rename/rename (2to1) + modify/modify' '
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
'

test_expect_success 'handle rename/rename (2to1) conflict correctly' '
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
		test_line_count = 3 out &&

		test_path_is_missing a &&
		test_path_is_missing b &&
		test_path_is_file c~HEAD &&
		test_path_is_file c~C^0 &&

		git rev-parse >expect   \
			C:a     B:b     &&
		git hash-object >actual \
			c~HEAD  c~C^0   &&
		test_cmp expect actual
	)
'

# Testcase setup for simple rename/rename (1to2) conflict:
#   Commit A: new file: a
#   Commit B: rename a->b
#   Commit C: rename a->c
test_expect_success 'setup simple rename/rename (1to2) conflict' '
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
'

test_expect_success 'merge has correct working tree contents' '
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

test_expect_success 'setup rename/rename(1to2)/add-source conflict' '
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
'

test_expect_failure 'detect conflict with rename/rename(1to2)/add-source merge' '
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
		test_cmp expect actual

		test_path_is_file a &&
		test_path_is_file b &&
		test_path_is_file c
	)
'

test_expect_success 'setup rename/rename(1to2)/add-source resolvable conflict' '
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
'

test_expect_failure 'rename/rename/add-source still tracks new a file' '
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

test_expect_success 'setup rename/rename(1to2)/add-dest conflict' '
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
'

test_expect_success 'rename/rename/add-dest merge still knows about conflicting file versions' '
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
		test_line_count = 5 out &&

		git rev-parse >expect               \
			A:a   C:b   B:b   C:c   B:c &&
		git rev-parse >actual                \
			:1:a  :2:b  :3:b  :2:c  :3:c &&
		test_cmp expect actual

		git rev-parse >expect               \
			C:c     B:c     C:b     B:b &&
		git hash-object >actual                \
			c~HEAD  c~B\^0  b~HEAD  b~B\^0 &&
		test_cmp expect actual

		test_path_is_missing b &&
		test_path_is_missing c
	)
'

test_done
