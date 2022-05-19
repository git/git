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
# of what cummits O, A, and B contain.
#
# Notation:
#    z/{b,c}   means  files z/b and z/c both exist
#    x/d_1     means  file x/d exists with content d1.  (Purpose of the
#                     underscore notation is to differentiate different
#                     files that might be renamed into each other's paths.)

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-merge.sh


###########################################################################
# SECTION 1: Cases involving no renames (one side has subset of changes of
#            the other side)
###########################################################################

# Testcase 1a, Changes on A, subset of changes on B
#   cummit O: b_1
#   cummit A: b_2
#   cummit B: b_3
#   Expected: b_2

test_setup_1a () {
	test_create_repo 1a_$1 &&
	(
		cd 1a_$1 &&

		test_write_lines 1 2 3 4 5 6 7 8 9 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "O" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 10.5 >b &&
		but add b &&
		test_tick &&
		but cummit -m "A" &&

		but checkout B &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "B"
	)
}

test_expect_success '1a-L: Modify(A)/Modify(B), change on B subset of A' '
	test_setup_1a L &&
	(
		cd 1a_L &&

		but checkout A^0 &&

		test-tool chmtime --get -3600 b >old-mtime &&

		GIT_MERGE_VERBOSITY=3 but merge -s recursive B^0 >out 2>err &&

		test_must_be_empty err &&

		# Make sure b was NOT updated
		test-tool chmtime --get b >new-mtime &&
		test_cmp old-mtime new-mtime &&

		but ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		but rev-parse >actual HEAD:b &&
		but rev-parse >expect A:b &&
		test_cmp expect actual &&

		but hash-object b   >actual &&
		but rev-parse   A:b >expect &&
		test_cmp expect actual
	)
'

test_expect_success '1a-R: Modify(A)/Modify(B), change on B subset of A' '
	test_setup_1a R &&
	(
		cd 1a_R &&

		but checkout B^0 &&

		test-tool chmtime --get -3600 b >old-mtime &&
		GIT_MERGE_VERBOSITY=3 but merge -s recursive A^0 >out 2>err &&

		# Make sure b WAS updated
		test-tool chmtime --get b >new-mtime &&
		test $(cat old-mtime) -lt $(cat new-mtime) &&

		test_must_be_empty err &&

		but ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		but rev-parse >actual HEAD:b &&
		but rev-parse >expect A:b &&
		test_cmp expect actual &&

		but hash-object b   >actual &&
		but rev-parse   A:b >expect &&
		test_cmp expect actual
	)
'


###########################################################################
# SECTION 2: Cases involving basic renames
###########################################################################

# Testcase 2a, Changes on A, rename on B
#   cummit O: b_1
#   cummit A: b_2
#   cummit B: c_1
#   Expected: c_2

test_setup_2a () {
	test_create_repo 2a_$1 &&
	(
		cd 2a_$1 &&

		test_seq 1 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "O" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		test_seq 1 11 >b &&
		but add b &&
		test_tick &&
		but cummit -m "A" &&

		but checkout B &&
		but mv b c &&
		test_tick &&
		but cummit -m "B"
	)
}

test_expect_success '2a-L: Modify/rename, merge into modify side' '
	test_setup_2a L &&
	(
		cd 2a_L &&

		but checkout A^0 &&

		test_path_is_missing c &&
		GIT_MERGE_VERBOSITY=3 but merge -s recursive B^0 >out 2>err &&

		test_path_is_file c &&

		but ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		but rev-parse >actual HEAD:c &&
		but rev-parse >expect A:b &&
		test_cmp expect actual &&

		but hash-object c   >actual &&
		but rev-parse   A:b >expect &&
		test_cmp expect actual &&

		test_must_fail but rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

test_expect_success '2a-R: Modify/rename, merge into rename side' '
	test_setup_2a R &&
	(
		cd 2a_R &&

		but checkout B^0 &&

		test-tool chmtime --get -3600 c >old-mtime &&
		GIT_MERGE_VERBOSITY=3 but merge -s recursive A^0 >out 2>err &&

		# Make sure c WAS updated
		test-tool chmtime --get c >new-mtime &&
		test $(cat old-mtime) -lt $(cat new-mtime) &&

		test_must_be_empty err &&

		but ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		but rev-parse >actual HEAD:c &&
		but rev-parse >expect A:b &&
		test_cmp expect actual &&

		but hash-object c   >actual &&
		but rev-parse   A:b >expect &&
		test_cmp expect actual &&

		test_must_fail but rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

# Testcase 2b, Changed and renamed on A, subset of changes on B
#   cummit O: b_1
#   cummit A: c_2
#   cummit B: b_3
#   Expected: c_2

test_setup_2b () {
	test_create_repo 2b_$1 &&
	(
		cd 2b_$1 &&

		test_write_lines 1 2 3 4 5 6 7 8 9 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "O" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 10.5 >b &&
		but add b &&
		but mv b c &&
		test_tick &&
		but cummit -m "A" &&

		but checkout B &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "B"
	)
}

test_expect_success '2b-L: Rename+Mod(A)/Mod(B), B mods subset of A' '
	test_setup_2b L &&
	(
		cd 2b_L &&

		but checkout A^0 &&

		test-tool chmtime --get -3600 c >old-mtime &&
		GIT_MERGE_VERBOSITY=3 but merge -s recursive B^0 >out 2>err &&

		test_must_be_empty err &&

		# Make sure c WAS updated
		test-tool chmtime --get c >new-mtime &&
		test_cmp old-mtime new-mtime &&

		but ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		but rev-parse >actual HEAD:c &&
		but rev-parse >expect A:c &&
		test_cmp expect actual &&

		but hash-object c   >actual &&
		but rev-parse   A:c >expect &&
		test_cmp expect actual &&

		test_must_fail but rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

test_expect_success '2b-R: Rename+Mod(A)/Mod(B), B mods subset of A' '
	test_setup_2b R &&
	(
		cd 2b_R &&

		but checkout B^0 &&

		test_path_is_missing c &&
		GIT_MERGE_VERBOSITY=3 but merge -s recursive A^0 >out 2>err &&

		# Make sure c now present (and thus was updated)
		test_path_is_file c &&

		test_must_be_empty err &&

		but ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		but rev-parse >actual HEAD:c &&
		but rev-parse >expect A:c &&
		test_cmp expect actual &&

		but hash-object c   >actual &&
		but rev-parse   A:c >expect &&
		test_cmp expect actual &&

		test_must_fail but rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

# Testcase 2c, Changes on A, rename on B
#   cummit O: b_1
#   cummit A: b_2, c_3
#   cummit B: c_1
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

test_setup_2c () {
	test_create_repo 2c &&
	(
		cd 2c &&

		test_seq 1 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "O" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		test_seq 1 11 >b &&
		echo whatever >c &&
		but add b c &&
		test_tick &&
		but cummit -m "A" &&

		but checkout B &&
		but mv b c &&
		test_tick &&
		but cummit -m "B"
	)
}

test_expect_success '2c: Modify b & add c VS rename b->c' '
	test_setup_2c &&
	(
		cd 2c &&

		but checkout A^0 &&

		test-tool chmtime --get -3600 c >old-mtime &&
		GIT_MERGE_VERBOSITY=3 &&
		export GIT_MERGE_VERBOSITY &&
		test_must_fail but merge -s recursive B^0 >out 2>err &&

		test_i18ngrep "CONFLICT (.*/add):" out &&
		test_must_be_empty err &&

		# Make sure c WAS updated
		test-tool chmtime --get c >new-mtime &&
		test $(cat old-mtime) -lt $(cat new-mtime)

		# FIXME: rename/add conflicts are horribly broken right now;
		# when I get back to my patch series fixing it and
		# rename/rename(2to1) conflicts to bring them in line with
		# how add/add conflicts behave, then checks like the below
		# could be added.  But that patch series is waiting until
		# the rename-directory-detection series lands, which this
		# is part of.  And in the mean time, I do not want to further
		# enforce broken behavior.  So for now, the main test is the
		# one above that err is an empty file.

		#but ls-files -s >index_files &&
		#test_line_count = 2 index_files &&

		#but rev-parse >actual :2:c :3:c &&
		#but rev-parse >expect A:b  A:c  &&
		#test_cmp expect actual &&

		#but cat-file -p A:b >>merged &&
		#but cat-file -p A:c >>merge-me &&
		#>empty &&
		#test_must_fail but merge-file \
		#	-L "Temporary merge branch 1" \
		#	-L "" \
		#	-L "Temporary merge branch 2" \
		#	merged empty merge-me &&
		#sed -e "s/^\([<=>]\)/\1\1\1/" merged >merged-internal &&

		#but hash-object c               >actual &&
		#but hash-object merged-internal >expect &&
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
#   cummit O: bq_1, foo/whatever
#   cummit A: foo/{bq_2, whatever}
#   cummit B: bq_1, bar/whatever
#   Expected: bar/{bq_2, whatever}

test_setup_3a () {
	test_create_repo 3a_$1 &&
	(
		cd 3a_$1 &&

		mkdir foo &&
		test_seq 1 10 >bq &&
		test_write_lines a b c d e f g h i j k >foo/whatever &&
		but add bq foo/whatever &&
		test_tick &&
		but cummit -m "O" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		test_seq 1 11 >bq &&
		but add bq &&
		but mv bq foo/ &&
		test_tick &&
		but cummit -m "A" &&

		but checkout B &&
		but mv foo/ bar/ &&
		test_tick &&
		but cummit -m "B"
	)
}

test_expect_success '3a-L: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_setup_3a L &&
	(
		cd 3a_L &&

		but checkout A^0 &&

		test_path_is_missing bar/bq &&
		GIT_MERGE_VERBOSITY=3 but -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&

		test_must_be_empty err &&

		test_path_is_file bar/bq &&

		but ls-files -s >index_files &&
		test_line_count = 2 index_files &&

		but rev-parse >actual HEAD:bar/bq HEAD:bar/whatever &&
		but rev-parse >expect A:foo/bq    A:foo/whatever &&
		test_cmp expect actual &&

		but hash-object bar/bq   bar/whatever   >actual &&
		but rev-parse   A:foo/bq A:foo/whatever >expect &&
		test_cmp expect actual &&

		test_must_fail but rev-parse HEAD:bq HEAD:foo/bq &&
		test_path_is_missing bq &&
		test_path_is_missing foo/bq &&
		test_path_is_missing foo/whatever
	)
'

test_expect_success '3a-R: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_setup_3a R &&
	(
		cd 3a_R &&

		but checkout B^0 &&

		test_path_is_missing bar/bq &&
		GIT_MERGE_VERBOSITY=3 but -c merge.directoryRenames=true merge -s recursive A^0 >out 2>err &&

		test_must_be_empty err &&

		test_path_is_file bar/bq &&

		but ls-files -s >index_files &&
		test_line_count = 2 index_files &&

		but rev-parse >actual HEAD:bar/bq HEAD:bar/whatever &&
		but rev-parse >expect A:foo/bq    A:foo/whatever &&
		test_cmp expect actual &&

		but hash-object bar/bq   bar/whatever   >actual &&
		but rev-parse   A:foo/bq A:foo/whatever >expect &&
		test_cmp expect actual &&

		test_must_fail but rev-parse HEAD:bq HEAD:foo/bq &&
		test_path_is_missing bq &&
		test_path_is_missing foo/bq &&
		test_path_is_missing foo/whatever
	)
'

# Testcase 3b, rename into dir foo on A, dir rename foo->bar + change on B
#   cummit O: bq_1, foo/whatever
#   cummit A: foo/{bq_1, whatever}
#   cummit B: bq_2, bar/whatever
#   Expected: bar/{bq_2, whatever}

test_setup_3b () {
	test_create_repo 3b_$1 &&
	(
		cd 3b_$1 &&

		mkdir foo &&
		test_seq 1 10 >bq &&
		test_write_lines a b c d e f g h i j k >foo/whatever &&
		but add bq foo/whatever &&
		test_tick &&
		but cummit -m "O" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		but mv bq foo/ &&
		test_tick &&
		but cummit -m "A" &&

		but checkout B &&
		test_seq 1 11 >bq &&
		but add bq &&
		but mv foo/ bar/ &&
		test_tick &&
		but cummit -m "B"
	)
}

test_expect_success '3b-L: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_setup_3b L &&
	(
		cd 3b_L &&

		but checkout A^0 &&

		test_path_is_missing bar/bq &&
		GIT_MERGE_VERBOSITY=3 but -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&

		test_must_be_empty err &&

		test_path_is_file bar/bq &&

		but ls-files -s >index_files &&
		test_line_count = 2 index_files &&

		but rev-parse >actual HEAD:bar/bq HEAD:bar/whatever &&
		but rev-parse >expect B:bq        A:foo/whatever &&
		test_cmp expect actual &&

		but hash-object bar/bq bar/whatever   >actual &&
		but rev-parse   B:bq   A:foo/whatever >expect &&
		test_cmp expect actual &&

		test_must_fail but rev-parse HEAD:bq HEAD:foo/bq &&
		test_path_is_missing bq &&
		test_path_is_missing foo/bq &&
		test_path_is_missing foo/whatever
	)
'

test_expect_success '3b-R: bq_1->foo/bq_2 on A, foo/->bar/ on B' '
	test_setup_3b R &&
	(
		cd 3b_R &&

		but checkout B^0 &&

		test_path_is_missing bar/bq &&
		GIT_MERGE_VERBOSITY=3 but -c merge.directoryRenames=true merge -s recursive A^0 >out 2>err &&

		test_must_be_empty err &&

		test_path_is_file bar/bq &&

		but ls-files -s >index_files &&
		test_line_count = 2 index_files &&

		but rev-parse >actual HEAD:bar/bq HEAD:bar/whatever &&
		but rev-parse >expect B:bq        A:foo/whatever &&
		test_cmp expect actual &&

		but hash-object bar/bq bar/whatever   >actual &&
		but rev-parse   B:bq   A:foo/whatever >expect &&
		test_cmp expect actual &&

		test_must_fail but rev-parse HEAD:bq HEAD:foo/bq &&
		test_path_is_missing bq &&
		test_path_is_missing foo/bq &&
		test_path_is_missing foo/whatever
	)
'

###########################################################################
# SECTION 4: Cases involving dirty changes
###########################################################################

# Testcase 4a, Changed on A, subset of changes on B, locally modified
#   cummit O: b_1
#   cummit A: b_2
#   cummit B: b_3
#   Working copy: b_4
#   Expected: b_2 for merge, b_4 in working copy

test_setup_4a () {
	test_create_repo 4a &&
	(
		cd 4a &&

		test_write_lines 1 2 3 4 5 6 7 8 9 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "O" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 10.5 >b &&
		but add b &&
		test_tick &&
		but cummit -m "A" &&

		but checkout B &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "B"
	)
}

# NOTE: For as long as we continue using unpack_trees() without index_only
#   set to true, it will error out on a case like this claiming that the locally
#   modified file would be overwritten by the merge.  Getting this testcase
#   correct requires doing the merge in-memory first, then realizing that no
#   updates to the file are necessary, and thus that we can just leave the path
#   alone.
test_expect_merge_algorithm failure success '4a: Change on A, change on B subset of A, dirty mods present' '
	test_setup_4a &&
	(
		cd 4a &&

		but checkout A^0 &&
		echo "File rewritten" >b &&

		test-tool chmtime --get -3600 b >old-mtime &&

		GIT_MERGE_VERBOSITY=3 but merge -s recursive B^0 >out 2>err &&

		test_must_be_empty err &&

		# Make sure b was NOT updated
		test-tool chmtime --get b >new-mtime &&
		test_cmp old-mtime new-mtime &&

		but ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		but rev-parse >actual :0:b &&
		but rev-parse >expect A:b &&
		test_cmp expect actual &&

		but hash-object b >actual &&
		echo "File rewritten" | but hash-object --stdin >expect &&
		test_cmp expect actual
	)
'

# Testcase 4b, Changed+renamed on A, subset of changes on B, locally modified
#   cummit O: b_1
#   cummit A: c_2
#   cummit B: b_3
#   Working copy: c_4
#   Expected: c_2

test_setup_4b () {
	test_create_repo 4b &&
	(
		cd 4b &&

		test_write_lines 1 2 3 4 5 6 7 8 9 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "O" &&

		but branch O &&
		but branch A &&
		but branch B &&

		but checkout A &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 10.5 >b &&
		but add b &&
		but mv b c &&
		test_tick &&
		but cummit -m "A" &&

		but checkout B &&
		test_write_lines 1 2 3 4 5 5.5 6 7 8 9 10 >b &&
		but add b &&
		test_tick &&
		but cummit -m "B"
	)
}

test_expect_success '4b: Rename+Mod(A)/Mod(B), change on B subset of A, dirty mods present' '
	test_setup_4b &&
	(
		cd 4b &&

		but checkout A^0 &&
		echo "File rewritten" >c &&

		test-tool chmtime --get -3600 c >old-mtime &&

		GIT_MERGE_VERBOSITY=3 but merge -s recursive B^0 >out 2>err &&

		test_must_be_empty err &&

		# Make sure c was NOT updated
		test-tool chmtime --get c >new-mtime &&
		test_cmp old-mtime new-mtime &&

		but ls-files -s >index_files &&
		test_line_count = 1 index_files &&

		but rev-parse >actual :0:c &&
		but rev-parse >expect A:c &&
		test_cmp expect actual &&

		but hash-object c >actual &&
		echo "File rewritten" | but hash-object --stdin >expect &&
		test_cmp expect actual &&

		test_must_fail but rev-parse HEAD:b &&
		test_path_is_missing b
	)
'

test_done
