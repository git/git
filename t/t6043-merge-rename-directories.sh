#!/bin/sh

test_description="recursive merge with directory renames"
# includes checking of many corner cases, with a similar methodology to:
#   t6042: corner cases with renames but not criss-cross merges
#   t6036: corner cases with both renames and criss-cross merges
#
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
# SECTION 1: Basic cases we should be able to handle
###########################################################################

# Testcase 1a, Basic directory rename.
#   Commit O: z/{b,c}
#   Commit A: y/{b,c}
#   Commit B: z/{b,c,d,e/f}
#   Expected: y/{b,c,d,e/f}

test_expect_success '1a-setup: Simple directory rename detection' '
	test_create_repo 1a &&
	(
		cd 1a &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z y &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo d >z/d &&
		mkdir z/e &&
		echo f >z/e/f &&
		git add z/d z/e/f &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_failure '1a-check: Simple directory rename detection' '
	(
		cd 1a &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/d HEAD:y/e/f &&
		git rev-parse >expect \
			O:z/b    O:z/c    B:z/d    B:z/e/f &&
		test_cmp expect actual &&

		git hash-object y/d >actual &&
		git rev-parse B:z/d >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:z/d &&
		test_must_fail git rev-parse HEAD:z/e/f &&
		test_path_is_missing z/d &&
		test_path_is_missing z/e/f
	)
'

# Testcase 1b, Merge a directory with another
#   Commit O: z/{b,c},   y/d
#   Commit A: z/{b,c,e}, y/d
#   Commit B: y/{b,c,d}
#   Expected: y/{b,c,d,e}

test_expect_success '1b-setup: Merge a directory with another' '
	test_create_repo 1b &&
	(
		cd 1b &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		mkdir y &&
		echo d >y/d &&
		git add z y &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		echo e >z/e &&
		git add z/e &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv z/b y &&
		git mv z/c y &&
		rmdir z &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_failure '1b-check: Merge a directory with another' '
	(
		cd 1b &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/d HEAD:y/e &&
		git rev-parse >expect \
			O:z/b    O:z/c    O:y/d    A:z/e &&
		test_cmp expect actual &&
		test_must_fail git rev-parse HEAD:z/e
	)
'

# Testcase 1c, Transitive renaming
#   (Related to testcases 3a and 6d -- when should a transitive rename apply?)
#   (Related to testcases 9c and 9d -- can transitivity repeat?)
#   Commit O: z/{b,c},   x/d
#   Commit A: y/{b,c},   x/d
#   Commit B: z/{b,c,d}
#   Expected: y/{b,c,d}  (because x/d -> z/d -> y/d)

test_expect_success '1c-setup: Transitive renaming' '
	test_create_repo 1c &&
	(
		cd 1c &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		mkdir x &&
		echo d >x/d &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z y &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/d z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_failure '1c-check: Transitive renaming' '
	(
		cd 1c &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/d &&
		git rev-parse >expect \
			O:z/b    O:z/c    O:x/d &&
		test_cmp expect actual &&
		test_must_fail git rev-parse HEAD:x/d &&
		test_must_fail git rev-parse HEAD:z/d &&
		test_path_is_missing z/d
	)
'

# Testcase 1d, Directory renames (merging two directories into one new one)
#              cause a rename/rename(2to1) conflict
#   (Related to testcases 1c and 7b)
#   Commit O. z/{b,c},        y/{d,e}
#   Commit A. x/{b,c},        y/{d,e,m,wham_1}
#   Commit B. z/{b,c,n,wham_2}, x/{d,e}
#   Expected: x/{b,c,d,e,m,n}, CONFLICT:(y/wham_1 & z/wham_2 -> x/wham)
#   Note: y/m & z/n should definitely move into x.  By the same token, both
#         y/wham_1 & z/wham_2 should too...giving us a conflict.

test_expect_success '1d-setup: Directory renames cause a rename/rename(2to1) conflict' '
	test_create_repo 1d &&
	(
		cd 1d &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		mkdir y &&
		echo d >y/d &&
		echo e >y/e &&
		git add z y &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z x &&
		echo m >y/m &&
		echo wham1 >y/wham &&
		git add y &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv y x &&
		echo n >z/n &&
		echo wham2 >z/wham &&
		git add z &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_failure '1d-check: Directory renames cause a rename/rename(2to1) conflict' '
	(
		cd 1d &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (rename/rename)" out &&

		git ls-files -s >out &&
		test_line_count = 8 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			:0:x/b :0:x/c :0:x/d :0:x/e :0:x/m :0:x/n &&
		git rev-parse >expect \
			 O:z/b  O:z/c  O:y/d  O:y/e  A:y/m  B:z/n &&
		test_cmp expect actual &&

		test_must_fail git rev-parse :0:x/wham &&
		git rev-parse >actual \
			:2:x/wham :3:x/wham &&
		git rev-parse >expect \
			 A:y/wham  B:z/wham &&
		test_cmp expect actual &&

		test_path_is_missing x/wham &&
		test_path_is_file x/wham~HEAD &&
		test_path_is_file x/wham~B^0 &&

		git hash-object >actual \
			x/wham~HEAD x/wham~B^0 &&
		git rev-parse >expect \
			A:y/wham    B:z/wham &&
		test_cmp expect actual
	)
'

# Testcase 1e, Renamed directory, with all filenames being renamed too
#   Commit O: z/{oldb,oldc}
#   Commit A: y/{newb,newc}
#   Commit B: z/{oldb,oldc,d}
#   Expected: y/{newb,newc,d}

test_expect_success '1e-setup: Renamed directory, with all files being renamed too' '
	test_create_repo 1e &&
	(
		cd 1e &&

		mkdir z &&
		echo b >z/oldb &&
		echo c >z/oldc &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir y &&
		git mv z/oldb y/newb &&
		git mv z/oldc y/newc &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo d >z/d &&
		git add z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_failure '1e-check: Renamed directory, with all files being renamed too' '
	(
		cd 1e &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			HEAD:y/newb HEAD:y/newc HEAD:y/d &&
		git rev-parse >expect \
			O:z/oldb    O:z/oldc    B:z/d &&
		test_cmp expect actual &&
		test_must_fail git rev-parse HEAD:z/d
	)
'

# Testcase 1f, Split a directory into two other directories
#   (Related to testcases 3a, all of section 2, and all of section 4)
#   Commit O: z/{b,c,d,e,f}
#   Commit A: z/{b,c,d,e,f,g}
#   Commit B: y/{b,c}, x/{d,e,f}
#   Expected: y/{b,c}, x/{d,e,f,g}

test_expect_success '1f-setup: Split a directory into two other directories' '
	test_create_repo 1f &&
	(
		cd 1f &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		echo d >z/d &&
		echo e >z/e &&
		echo f >z/f &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		echo g >z/g &&
		git add z/g &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		mkdir y &&
		mkdir x &&
		git mv z/b y/ &&
		git mv z/c y/ &&
		git mv z/d x/ &&
		git mv z/e x/ &&
		git mv z/f x/ &&
		rmdir z &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_failure '1f-check: Split a directory into two other directories' '
	(
		cd 1f &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 6 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:x/d HEAD:x/e HEAD:x/f HEAD:x/g &&
		git rev-parse >expect \
			O:z/b    O:z/c    O:z/d    O:z/e    O:z/f    A:z/g &&
		test_cmp expect actual &&
		test_path_is_missing z/g &&
		test_must_fail git rev-parse HEAD:z/g
	)
'

###########################################################################
# Rules suggested by testcases in section 1:
#
#   We should still detect the directory rename even if it wasn't just
#   the directory renamed, but the files within it. (see 1b)
#
#   If renames split a directory into two or more others, the directory
#   with the most renames, "wins" (see 1c).  However, see the testcases
#   in section 2, plus testcases 3a and 4a.
###########################################################################


###########################################################################
# SECTION 2: Split into multiple directories, with equal number of paths
#
# Explore the splitting-a-directory rules a bit; what happens in the
# edge cases?
#
# Note that there is a closely related case of a directory not being
# split on either side of history, but being renamed differently on
# each side.  See testcase 8e for that.
###########################################################################

# Testcase 2a, Directory split into two on one side, with equal numbers of paths
#   Commit O: z/{b,c}
#   Commit A: y/b, w/c
#   Commit B: z/{b,c,d}
#   Expected: y/b, w/c, z/d, with warning about z/ -> (y/ vs. w/) conflict
test_expect_success '2a-setup: Directory split into two on one side, with equal numbers of paths' '
	test_create_repo 2a &&
	(
		cd 2a &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir y &&
		mkdir w &&
		git mv z/b y/ &&
		git mv z/c w/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo d >z/d &&
		git add z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_failure '2a-check: Directory split into two on one side, with equal numbers of paths' '
	(
		cd 2a &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT.*directory rename split" out &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :0:w/c :0:z/d &&
		git rev-parse >expect \
			 O:z/b  O:z/c  B:z/d &&
		test_cmp expect actual
	)
'

# Testcase 2b, Directory split into two on one side, with equal numbers of paths
#   Commit O: z/{b,c}
#   Commit A: y/b, w/c
#   Commit B: z/{b,c}, x/d
#   Expected: y/b, w/c, x/d; No warning about z/ -> (y/ vs. w/) conflict
test_expect_success '2b-setup: Directory split into two on one side, with equal numbers of paths' '
	test_create_repo 2b &&
	(
		cd 2b &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir y &&
		mkdir w &&
		git mv z/b y/ &&
		git mv z/c w/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		mkdir x &&
		echo d >x/d &&
		git add x/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '2b-check: Directory split into two on one side, with equal numbers of paths' '
	(
		cd 2b &&

		git checkout A^0 &&

		git merge -s recursive B^0 >out &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :0:w/c :0:x/d &&
		git rev-parse >expect \
			 O:z/b  O:z/c  B:x/d &&
		test_cmp expect actual &&
		test_i18ngrep ! "CONFLICT.*directory rename split" out
	)
'

###########################################################################
# Rules suggested by section 2:
#
#   None; the rule was already covered in section 1.  These testcases are
#   here just to make sure the conflict resolution and necessary warning
#   messages are handled correctly.
###########################################################################


###########################################################################
# SECTION 3: Path in question is the source path for some rename already
#
# Combining cases from Section 1 and trying to handle them could lead to
# directory renaming detection being over-applied.  So, this section
# provides some good testcases to check that the implementation doesn't go
# too far.
###########################################################################

# Testcase 3a, Avoid implicit rename if involved as source on other side
#   (Related to testcases 1c and 1f)
#   Commit O: z/{b,c,d}
#   Commit A: z/{b,c,d} (no change)
#   Commit B: y/{b,c}, x/d
#   Expected: y/{b,c}, x/d
test_expect_success '3a-setup: Avoid implicit rename if involved as source on other side' '
	test_create_repo 3a &&
	(
		cd 3a &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		echo d >z/d &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_tick &&
		git commit --allow-empty -m "A" &&

		git checkout B &&
		mkdir y &&
		mkdir x &&
		git mv z/b y/ &&
		git mv z/c y/ &&
		git mv z/d x/ &&
		rmdir z &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '3a-check: Avoid implicit rename if involved as source on other side' '
	(
		cd 3a &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:x/d &&
		git rev-parse >expect \
			O:z/b    O:z/c    O:z/d &&
		test_cmp expect actual
	)
'

# Testcase 3b, Avoid implicit rename if involved as source on other side
#   (Related to testcases 5c and 7c, also kind of 1e and 1f)
#   Commit O: z/{b,c,d}
#   Commit A: y/{b,c}, x/d
#   Commit B: z/{b,c}, w/d
#   Expected: y/{b,c}, CONFLICT:(z/d -> x/d vs. w/d)
#   NOTE: We're particularly checking that since z/d is already involved as
#         a source in a file rename on the same side of history, that we don't
#         get it involved in directory rename detection.  If it were, we might
#         end up with CONFLICT:(z/d -> y/d vs. x/d vs. w/d), i.e. a
#         rename/rename/rename(1to3) conflict, which is just weird.
test_expect_success '3b-setup: Avoid implicit rename if involved as source on current side' '
	test_create_repo 3b &&
	(
		cd 3b &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		echo d >z/d &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir y &&
		mkdir x &&
		git mv z/b y/ &&
		git mv z/c y/ &&
		git mv z/d x/ &&
		rmdir z &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		mkdir w &&
		git mv z/d w/ &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '3b-check: Avoid implicit rename if involved as source on current side' '
	(
		cd 3b &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep CONFLICT.*rename/rename.*z/d.*x/d.*w/d out &&
		test_i18ngrep ! CONFLICT.*rename/rename.*y/d out &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :1:z/d :2:x/d :3:w/d &&
		git rev-parse >expect \
			 O:z/b  O:z/c  O:z/d  O:z/d  O:z/d &&
		test_cmp expect actual &&

		test_path_is_missing z/d &&
		git hash-object >actual \
			x/d   w/d &&
		git rev-parse >expect \
			O:z/d O:z/d &&
		test_cmp expect actual
	)
'

###########################################################################
# Rules suggested by section 3:
#
#   Avoid directory-rename-detection for a path, if that path is the source
#   of a rename on either side of a merge.
###########################################################################


###########################################################################
# SECTION 4: Partially renamed directory; still exists on both sides of merge
#
# What if we were to attempt to do directory rename detection when someone
# "mostly" moved a directory but still left some files around, or,
# equivalently, fully renamed a directory in one commmit and then recreated
# that directory in a later commit adding some new files and then tried to
# merge?
#
# It's hard to divine user intent in these cases, because you can make an
# argument that, depending on the intermediate history of the side being
# merged, that some users will want files in that directory to
# automatically be detected and renamed, while users with a different
# intermediate history wouldn't want that rename to happen.
#
# I think that it is best to simply not have directory rename detection
# apply to such cases.  My reasoning for this is four-fold: (1) it's
# easiest for users in general to figure out what happened if we don't
# apply directory rename detection in any such case, (2) it's an easy rule
# to explain ["We don't do directory rename detection if the directory
# still exists on both sides of the merge"], (3) we can get some hairy
# edge/corner cases that would be really confusing and possibly not even
# representable in the index if we were to even try, and [related to 3] (4)
# attempting to resolve this issue of divining user intent by examining
# intermediate history goes against the spirit of three-way merges and is a
# path towards crazy corner cases that are far more complex than what we're
# already dealing with.
#
# Note that the wording of the rule ("We don't do directory rename
# detection if the directory still exists on both sides of the merge.")
# also excludes "renaming" of a directory into a subdirectory of itself
# (e.g. /some/dir/* -> /some/dir/subdir/*).  It may be possible to carve
# out an exception for "renaming"-beneath-itself cases without opening
# weird edge/corner cases for other partial directory renames, but for now
# we are keeping the rule simple.
#
# This section contains a test for a partially-renamed-directory case.
###########################################################################

# Testcase 4a, Directory split, with original directory still present
#   (Related to testcase 1f)
#   Commit O: z/{b,c,d,e}
#   Commit A: y/{b,c,d}, z/e
#   Commit B: z/{b,c,d,e,f}
#   Expected: y/{b,c,d}, z/{e,f}
#   NOTE: Even though most files from z moved to y, we don't want f to follow.

test_expect_success '4a-setup: Directory split, with original directory still present' '
	test_create_repo 4a &&
	(
		cd 4a &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		echo d >z/d &&
		echo e >z/e &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir y &&
		git mv z/b y/ &&
		git mv z/c y/ &&
		git mv z/d y/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo f >z/f &&
		git add z/f &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '4a-check: Directory split, with original directory still present' '
	(
		cd 4a &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/d HEAD:z/e HEAD:z/f &&
		git rev-parse >expect \
			O:z/b    O:z/c    O:z/d    O:z/e    B:z/f &&
		test_cmp expect actual
	)
'

###########################################################################
# Rules suggested by section 4:
#
#   Directory-rename-detection should be turned off for any directories (as
#   a source for renames) that exist on both sides of the merge.  (The "as
#   a source for renames" clarification is due to cases like 1c where
#   the target directory exists on both sides and we do want the rename
#   detection.)  But, sadly, see testcase 8b.
###########################################################################

test_done
