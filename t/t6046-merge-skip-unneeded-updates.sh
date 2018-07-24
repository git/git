#!/bin/sh

test_description="merge cases"

# The setup for all of them, pictorially, is:
#
#      A
#      o
#     / \
#  O o   ?
#     \ /
#      o
#      B
#
# To help make it easier to follow the flow of tests, they have been
# divided into sections and each test will start with a quick explanation
# of what commits O, A, and B contain.
#
# Notation:
#    z/{b,c}   means  files z/b and z/c both exist
#    x/d_1     means  file x/d exists with content d1.  (Purpose of the
#                     underscore notation is to differentiate different
#                     files that might be renamed into each other's paths.)

. ./test-lib.sh


###########################################################################
# SECTION 1: Cases involving no renames (one side has subset of changes of
#            the other side)
###########################################################################

# Testcase 1a, Changes on A, subset of changes on B
#   Commit O: b_1
#   Commit A: b_2
#   Commit B: b_3
#   Expected: b_2

test_expect_success '1a-setup: Modify(A)/Modify(B), change on B subset of A' '
	test_create_repo 1a &&
	(
		cd 1a &&

		test_write_lines 1 2 3 4 5 6 7 8 9 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 10.5 >b &&
		git add b &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '1a-check-L: Modify(A)/Modify(B), change on B subset of A' '
	test_when_finished "git -C 1a reset --hard" &&
	test_when_finished "git -C 1a clean -fd" &&
	(
		cd 1a &&

		git checkout A^0 &&

		test-tool chmtime =31337 b &&
		test-tool chmtime -v +0 b >expected-mtime &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive B^0 >out 2>err &&

		test_i18ngrep "Skipped b" out &&
		test_must_be_empty err &&

		test-tool chmtime -v +0 b >actual-mtime &&
		test_cmp expected-mtime actual-mtime &&

		git ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		git rev-parse >actual HEAD:b &&
		git rev-parse >expect A:b &&
		test_cmp expect actual &&

		git hash-object b   >actual &&
		git rev-parse   A:b >expect &&
		test_cmp expect actual
	)
'

test_expect_success '1a-check-R: Modify(A)/Modify(B), change on B subset of A' '
	test_when_finished "git -C 1a reset --hard" &&
	test_when_finished "git -C 1a clean -fd" &&
	(
		cd 1a &&

		git checkout B^0 &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive A^0 >out 2>err &&

		test_i18ngrep "Auto-merging b" out &&
		test_must_be_empty err &&

		git ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		git rev-parse >actual HEAD:b &&
		git rev-parse >expect A:b &&
		test_cmp expect actual &&

		git hash-object b   >actual &&
		git rev-parse   A:b >expect &&
		test_cmp expect actual
	)
'


###########################################################################
# SECTION 2: Cases involving basic renames
###########################################################################

# Testcase 2a, Changes on A, rename on B
#   Commit O: b_1
#   Commit A: b_2
#   Commit B: c_1
#   Expected: c_2

test_expect_success '2a-setup: Modify(A)/rename(B)' '
	test_create_repo 2a &&
	(
		cd 2a &&

		test_seq 1 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_seq 1 11 >b &&
		git add b &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv b c &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '2a-check-L: Modify/rename, merge into modify side' '
	test_when_finished "git -C 2a reset --hard" &&
	test_when_finished "git -C 2a clean -fd" &&
	(
		cd 2a &&

		git checkout A^0 &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive B^0 >out 2>err &&

		test_i18ngrep ! "Skipped c" out &&
		test_must_be_empty err &&

		git ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		git rev-parse >actual HEAD:c &&
		git rev-parse >expect A:b &&
		test_cmp expect actual &&

		git hash-object c   >actual &&
		git rev-parse   A:b >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

test_expect_success '2a-check-R: Modify/rename, merge into rename side' '
	test_when_finished "git -C 2a reset --hard" &&
	test_when_finished "git -C 2a clean -fd" &&
	(
		cd 2a &&

		git checkout B^0 &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive A^0 >out 2>err &&

		test_i18ngrep ! "Skipped c" out &&
		test_must_be_empty err &&

		git ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		git rev-parse >actual HEAD:c &&
		git rev-parse >expect A:b &&
		test_cmp expect actual &&

		git hash-object c   >actual &&
		git rev-parse   A:b >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

# Testcase 2b, Changed and renamed on A, subset of changes on B
#   Commit O: b_1
#   Commit A: c_2
#   Commit B: b_3
#   Expected: c_2

test_expect_success '2b-setup: Rename+Mod(A)/Mod(B), B mods subset of A' '
	test_create_repo 2b &&
	(
		cd 2b &&

		test_write_lines 1 2 3 4 5 6 7 8 9 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 10.5 >b &&
		git add b &&
		git mv b c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '2b-check-L: Rename+Mod(A)/Mod(B), B mods subset of A' '
	test_when_finished "git -C 2b reset --hard" &&
	test_when_finished "git -C 2b clean -fd" &&
	(
		cd 2b &&

		git checkout A^0 &&

		test-tool chmtime =31337 c &&
		test-tool chmtime -v +0 c >expected-mtime &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive B^0 >out 2>err &&

		test_i18ngrep "Skipped c" out &&
		test_must_be_empty err &&

		test-tool chmtime -v +0 c >actual-mtime &&
		test_cmp expected-mtime actual-mtime &&

		git ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		git rev-parse >actual HEAD:c &&
		git rev-parse >expect A:c &&
		test_cmp expect actual &&

		git hash-object c   >actual &&
		git rev-parse   A:c >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

test_expect_success '2b-check-R: Rename+Mod(A)/Mod(B), B mods subset of A' '
	test_when_finished "git -C 2b reset --hard" &&
	test_when_finished "git -C 2b clean -fd" &&
	(
		cd 2b &&

		git checkout B^0 &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive A^0 >out 2>err &&

		test_i18ngrep "Auto-merging c" out &&
		test_must_be_empty err &&

		git ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		git rev-parse >actual HEAD:c &&
		git rev-parse >expect A:c &&
		test_cmp expect actual &&

		git hash-object c   >actual &&
		git rev-parse   A:c >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

# Testcase 2c, Changes on A, rename on B
#   Commit O: b_1
#   Commit A: b_2, c_3
#   Commit B: c_1
#   Expected: rename/add conflict c_2 vs c_3
#
#   NOTE: Since A modified b_1->b_2, and B renamed b_1->c_1, the threeway
#         merge of those files should result in c_2.  We then should have a
#         rename/add conflict between c_2 and c_3.  However, if we note in
#         merge_content() that A had the right contents (b_2 has same
#         contents as c_2, just at a different name), and that A had the
#         right path present (c_3 existed) and thus decides that it can
#         skip the update, then we're in trouble.  This test verifies we do
#         not make that particular mistake.

test_expect_success '2c-setup: Modify b & add c VS rename b->c' '
	test_create_repo 2c &&
	(
		cd 2c &&

		test_seq 1 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_seq 1 11 >b &&
		echo whatever >c &&
		git add b c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv b c &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '2c-check: Modify b & add c VS rename b->c' '
	(
		cd 2c &&

		git checkout A^0 &&

		GIT_MERGE_VERBOSITY=3 &&
		export GIT_MERGE_VERBOSITY &&
		test_must_fail git merge -s recursive B^0 >out 2>err &&

		test_i18ngrep "CONFLICT (rename/add): Rename b->c" out &&
		test_i18ngrep ! "Skipped c" out &&
		test_must_be_empty err

		# FIXME: rename/add conflicts are horribly broken right now;
		# when I get back to my patch series fixing it and
		# rename/rename(2to1) conflicts to bring them in line with
		# how add/add conflicts behave, then checks like the below
		# could be added.  But that patch series is waiting until
		# the rename-directory-detection series lands, which this
		# is part of.  And in the mean time, I do not want to further
		# enforce broken behavior.  So for now, the main test is the
		# one above that err is an empty file.

		#git ls-files -s >index_files &&
		#test_line_count = 2 index_files &&

		#git rev-parse >actual :2:c :3:c &&
		#git rev-parse >expect A:b  A:c  &&
		#test_cmp expect actual &&

		#git cat-file -p A:b >>merged &&
		#git cat-file -p A:c >>merge-me &&
		#>empty &&
		#test_must_fail git merge-file \
		#	-L "Temporary merge branch 1" \
		#	-L "" \
		#	-L "Temporary merge branch 2" \
		#	merged empty merge-me &&
		#sed -e "s/^\([<=>]\)/\1\1\1/" merged >merged-internal &&

		#git hash-object c               >actual &&
		#git hash-object merged-internal >expect &&
		#test_cmp expect actual &&

		#test_path_is_missing b
	)
'


###########################################################################
# SECTION 3: Cases involving directory renames
#
# NOTE:
#   Directory renames only apply when one side renames a directory, and the
#   other side adds or renames a path into that directory.  Applying the
#   directory rename to that new path creates a new pathname that didn't
#   exist on either side of history.  Thus, it is impossible for the
#   merge contents to already be at the right path, so all of these checks
#   exist just to make sure that updates are not skipped.
###########################################################################

# Testcase 3a, Change + rename into dir foo on A, dir rename foo->bar on B
#   Commit O: bq_1, foo/whatever
#   Commit A: foo/{bq_2, whatever}
#   Commit B: bq_1, bar/whatever
#   Expected: bar/{bq_2, whatever}

test_expect_success '3a-setup: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_create_repo 3a &&
	(
		cd 3a &&

		mkdir foo &&
		test_seq 1 10 >bq &&
		test_write_lines a b c d e f g h i j k >foo/whatever &&
		git add bq foo/whatever &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_seq 1 11 >bq &&
		git add bq &&
		git mv bq foo/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv foo/ bar/ &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '3a-check-L: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_when_finished "git -C 3a reset --hard" &&
	test_when_finished "git -C 3a clean -fd" &&
	(
		cd 3a &&

		git checkout A^0 &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive B^0 >out 2>err &&

		test_i18ngrep ! "Skipped bar/bq" out &&
		test_must_be_empty err &&

		git ls-files -s >index_files &&
		test_line_count = 2 index_files &&

		git rev-parse >actual HEAD:bar/bq HEAD:bar/whatever &&
		git rev-parse >expect A:foo/bq    A:foo/whatever &&
		test_cmp expect actual &&

		git hash-object bar/bq   bar/whatever   >actual &&
		git rev-parse   A:foo/bq A:foo/whatever >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:bq HEAD:foo/bq &&
		test_path_is_missing bq foo/bq foo/whatever
	)
'

test_expect_success '3a-check-R: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_when_finished "git -C 3a reset --hard" &&
	test_when_finished "git -C 3a clean -fd" &&
	(
		cd 3a &&

		git checkout B^0 &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive A^0 >out 2>err &&

		test_i18ngrep ! "Skipped bar/bq" out &&
		test_must_be_empty err &&

		git ls-files -s >index_files &&
		test_line_count = 2 index_files &&

		git rev-parse >actual HEAD:bar/bq HEAD:bar/whatever &&
		git rev-parse >expect A:foo/bq    A:foo/whatever &&
		test_cmp expect actual &&

		git hash-object bar/bq   bar/whatever   >actual &&
		git rev-parse   A:foo/bq A:foo/whatever >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:bq HEAD:foo/bq &&
		test_path_is_missing bq foo/bq foo/whatever
	)
'

# Testcase 3b, rename into dir foo on A, dir rename foo->bar + change on B
#   Commit O: bq_1, foo/whatever
#   Commit A: foo/{bq_1, whatever}
#   Commit B: bq_2, bar/whatever
#   Expected: bar/{bq_2, whatever}

test_expect_success '3b-setup: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_create_repo 3b &&
	(
		cd 3b &&

		mkdir foo &&
		test_seq 1 10 >bq &&
		test_write_lines a b c d e f g h i j k >foo/whatever &&
		git add bq foo/whatever &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv bq foo/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		test_seq 1 11 >bq &&
		git add bq &&
		git mv foo/ bar/ &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '3b-check-L: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_when_finished "git -C 3b reset --hard" &&
	test_when_finished "git -C 3b clean -fd" &&
	(
		cd 3b &&

		git checkout A^0 &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive B^0 >out 2>err &&

		test_i18ngrep ! "Skipped bar/bq" out &&
		test_must_be_empty err &&

		git ls-files -s >index_files &&
		test_line_count = 2 index_files &&

		git rev-parse >actual HEAD:bar/bq HEAD:bar/whatever &&
		git rev-parse >expect B:bq        A:foo/whatever &&
		test_cmp expect actual &&

		git hash-object bar/bq bar/whatever   >actual &&
		git rev-parse   B:bq   A:foo/whatever >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:bq HEAD:foo/bq &&
		test_path_is_missing bq foo/bq foo/whatever
	)
'

test_expect_success '3b-check-R: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_when_finished "git -C 3b reset --hard" &&
	test_when_finished "git -C 3b clean -fd" &&
	(
		cd 3b &&

		git checkout B^0 &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive A^0 >out 2>err &&

		test_i18ngrep ! "Skipped bar/bq" out &&
		test_must_be_empty err &&

		git ls-files -s >index_files &&
		test_line_count = 2 index_files &&

		git rev-parse >actual HEAD:bar/bq HEAD:bar/whatever &&
		git rev-parse >expect B:bq        A:foo/whatever &&
		test_cmp expect actual &&

		git hash-object bar/bq bar/whatever   >actual &&
		git rev-parse   B:bq   A:foo/whatever >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:bq HEAD:foo/bq &&
		test_path_is_missing bq foo/bq foo/whatever
	)
'

###########################################################################
# SECTION 4: Cases involving dirty changes
###########################################################################

# Testcase 4a, Changed on A, subset of changes on B, locally modified
#   Commit O: b_1
#   Commit A: b_2
#   Commit B: b_3
#   Working copy: b_4
#   Expected: b_2 for merge, b_4 in working copy

test_expect_success '4a-setup: Change on A, change on B subset of A, dirty mods present' '
	test_create_repo 4a &&
	(
		cd 4a &&

		test_write_lines 1 2 3 4 5 6 7 8 9 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 10.5 >b &&
		git add b &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "B"
	)
'

# NOTE: For as long as we continue using unpack_trees() without index_only
#   set to true, it will error out on a case like this claiming the the locally
#   modified file would be overwritten by the merge.  Getting this testcase
#   correct requires doing the merge in-memory first, then realizing that no
#   updates to the file are necessary, and thus that we can just leave the path
#   alone.
test_expect_failure '4a-check: Change on A, change on B subset of A, dirty mods present' '
	test_when_finished "git -C 4a reset --hard" &&
	test_when_finished "git -C 4a clean -fd" &&
	(
		cd 4a &&

		git checkout A^0 &&
		echo "File rewritten" >b &&

		test-tool chmtime =31337 b &&
		test-tool chmtime -v +0 b >expected-mtime &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive B^0 >out 2>err &&

		test_i18ngrep "Skipped b" out &&
		test_must_be_empty err &&

		test-tool chmtime -v +0 b >actual-mtime &&
		test_cmp expected-mtime actual-mtime &&

		git ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		git rev-parse >actual :0:b &&
		git rev-parse >expect A:b &&
		test_cmp expect actual &&

		git hash-object b >actual &&
		echo "File rewritten" | git hash-object --stdin >expect &&
		test_cmp expect actual
	)
'

# Testcase 4b, Changed+renamed on A, subset of changes on B, locally modified
#   Commit O: b_1
#   Commit A: c_2
#   Commit B: b_3
#   Working copy: c_4
#   Expected: c_2

test_expect_success '4b-setup: Rename+Mod(A)/Mod(B), change on B subset of A, dirty mods present' '
	test_create_repo 4b &&
	(
		cd 4b &&

		test_write_lines 1 2 3 4 5 6 7 8 9 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 10.5 >b &&
		git add b &&
		git mv b c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 >b &&
		git add b &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '4b-check: Rename+Mod(A)/Mod(B), change on B subset of A, dirty mods present' '
	test_when_finished "git -C 4b reset --hard" &&
	test_when_finished "git -C 4b clean -fd" &&
	(
		cd 4b &&

		git checkout A^0 &&
		echo "File rewritten" >c &&

		test-tool chmtime =31337 c &&
		test-tool chmtime -v +0 c >expected-mtime &&

		GIT_MERGE_VERBOSITY=3 git merge -s recursive B^0 >out 2>err &&

		test_i18ngrep "Skipped c" out &&
		test_must_be_empty err &&

		test-tool chmtime -v +0 c >actual-mtime &&
		test_cmp expected-mtime actual-mtime &&

		git ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		git rev-parse >actual :0:c &&
		git rev-parse >expect A:c &&
		test_cmp expect actual &&

		git hash-object c >actual &&
		echo "File rewritten" | git hash-object --stdin >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

test_done
