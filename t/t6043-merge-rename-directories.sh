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

test_expect_success '1a-check: Simple directory rename detection' '
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

test_expect_success '1b-check: Merge a directory with another' '
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
#   (Related to testcase 12b -- joint-transitivity?)
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

test_expect_success '1c-check: Transitive renaming' '
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

test_expect_success '1d-check: Directory renames cause a rename/rename(2to1) conflict' '
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
#   (Related to testcases 9f & 9g)
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

test_expect_success '1e-check: Renamed directory, with all files being renamed too' '
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

test_expect_success '1f-check: Split a directory into two other directories' '
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

test_expect_success '2a-check: Directory split into two on one side, with equal numbers of paths' '
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
#   (Related to testcases 1c, 1f, and 9h)
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


###########################################################################
# SECTION 5: Files/directories in the way of subset of to-be-renamed paths
#
# Implicitly renaming files due to a detected directory rename could run
# into problems if there are files or directories in the way of the paths
# we want to rename.  Explore such cases in this section.
###########################################################################

# Testcase 5a, Merge directories, other side adds files to original and target
#   Commit O: z/{b,c},       y/d
#   Commit A: z/{b,c,e_1,f}, y/{d,e_2}
#   Commit B: y/{b,c,d}
#   Expected: z/e_1, y/{b,c,d,e_2,f} + CONFLICT warning
#   NOTE: While directory rename detection is active here causing z/f to
#         become y/f, we did not apply this for z/e_1 because that would
#         give us an add/add conflict for y/e_1 vs y/e_2.  This problem with
#         this add/add, is that both versions of y/e are from the same side
#         of history, giving us no way to represent this conflict in the
#         index.

test_expect_success '5a-setup: Merge directories, other side adds files to original and target' '
	test_create_repo 5a &&
	(
		cd 5a &&

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
		echo e1 >z/e &&
		echo f >z/f &&
		echo e2 >y/e &&
		git add z/e z/f y/e &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv z/b y/ &&
		git mv z/c y/ &&
		rmdir z &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '5a-check: Merge directories, other side adds files to original and target' '
	(
		cd 5a &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT.*implicit dir rename" out &&

		git ls-files -s >out &&
		test_line_count = 6 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :0:y/d :0:y/e :0:z/e :0:y/f &&
		git rev-parse >expect \
			 O:z/b  O:z/c  O:y/d  A:y/e  A:z/e  A:z/f &&
		test_cmp expect actual
	)
'

# Testcase 5b, Rename/delete in order to get add/add/add conflict
#   (Related to testcase 8d; these may appear slightly inconsistent to users;
#    Also related to testcases 7d and 7e)
#   Commit O: z/{b,c,d_1}
#   Commit A: y/{b,c,d_2}
#   Commit B: z/{b,c,d_1,e}, y/d_3
#   Expected: y/{b,c,e}, CONFLICT(add/add: y/d_2 vs. y/d_3)
#   NOTE: If z/d_1 in commit B were to be involved in dir rename detection, as
#         we normaly would since z/ is being renamed to y/, then this would be
#         a rename/delete (z/d_1 -> y/d_1 vs. deleted) AND an add/add/add
#         conflict of y/d_1 vs. y/d_2 vs. y/d_3.  Add/add/add is not
#         representable in the index, so the existence of y/d_3 needs to
#         cause us to bail on directory rename detection for that path, falling
#         back to git behavior without the directory rename detection.

test_expect_success '5b-setup: Rename/delete in order to get add/add/add conflict' '
	test_create_repo 5b &&
	(
		cd 5b &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		echo d1 >z/d &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git rm z/d &&
		git mv z y &&
		echo d2 >y/d &&
		git add y/d &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		mkdir y &&
		echo d3 >y/d &&
		echo e >z/e &&
		git add y/d z/e &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '5b-check: Rename/delete in order to get add/add/add conflict' '
	(
		cd 5b &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (add/add).* y/d" out &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :0:y/e :2:y/d :3:y/d &&
		git rev-parse >expect \
			 O:z/b  O:z/c  B:z/e  A:y/d  B:y/d &&
		test_cmp expect actual &&

		test_must_fail git rev-parse :1:y/d &&
		test_path_is_file y/d
	)
'

# Testcase 5c, Transitive rename would cause rename/rename/rename/add/add/add
#   (Directory rename detection would result in transitive rename vs.
#    rename/rename(1to2) and turn it into a rename/rename(1to3).  Further,
#    rename paths conflict with separate adds on the other side)
#   (Related to testcases 3b and 7c)
#   Commit O: z/{b,c}, x/d_1
#   Commit A: y/{b,c,d_2}, w/d_1
#   Commit B: z/{b,c,d_1,e}, w/d_3, y/d_4
#   Expected: A mess, but only a rename/rename(1to2)/add/add mess.  Use the
#             presence of y/d_4 in B to avoid doing transitive rename of
#             x/d_1 -> z/d_1 -> y/d_1, so that the only paths we have at
#             y/d are y/d_2 and y/d_4.  We still do the move from z/e to y/e,
#             though, because it doesn't have anything in the way.

test_expect_success '5c-setup: Transitive rename would cause rename/rename/rename/add/add/add' '
	test_create_repo 5c &&
	(
		cd 5c &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		mkdir x &&
		echo d1 >x/d &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z y &&
		echo d2 >y/d &&
		git add y/d &&
		git mv x w &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/d z/ &&
		mkdir w &&
		mkdir y &&
		echo d3 >w/d &&
		echo d4 >y/d &&
		echo e >z/e &&
		git add w/ y/ z/e &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '5c-check: Transitive rename would cause rename/rename/rename/add/add/add' '
	(
		cd 5c &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (rename/rename).*x/d.*w/d.*z/d" out &&
		test_i18ngrep "CONFLICT (add/add).* y/d" out &&

		git ls-files -s >out &&
		test_line_count = 9 out &&
		git ls-files -u >out &&
		test_line_count = 6 out &&
		git ls-files -o >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :0:y/e &&
		git rev-parse >expect \
			 O:z/b  O:z/c  B:z/e &&
		test_cmp expect actual &&

		test_must_fail git rev-parse :1:y/d &&
		git rev-parse >actual \
			:2:w/d :3:w/d :1:x/d :2:y/d :3:y/d :3:z/d &&
		git rev-parse >expect \
			 O:x/d  B:w/d  O:x/d  A:y/d  B:y/d  O:x/d &&
		test_cmp expect actual &&

		git hash-object >actual \
			w/d~HEAD w/d~B^0 z/d &&
		git rev-parse >expect \
			O:x/d    B:w/d   O:x/d &&
		test_cmp expect actual &&
		test_path_is_missing x/d &&
		test_path_is_file y/d &&
		grep -q "<<<<" y/d  # conflict markers should be present
	)
'

# Testcase 5d, Directory/file/file conflict due to directory rename
#   Commit O: z/{b,c}
#   Commit A: y/{b,c,d_1}
#   Commit B: z/{b,c,d_2,f}, y/d/e
#   Expected: y/{b,c,d/e,f}, z/d_2, CONFLICT(file/directory), y/d_1~HEAD
#   Note: The fact that y/d/ exists in B makes us bail on directory rename
#         detection for z/d_2, but that doesn't prevent us from applying the
#         directory rename detection for z/f -> y/f.

test_expect_success '5d-setup: Directory/file/file conflict due to directory rename' '
	test_create_repo 5d &&
	(
		cd 5d &&

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
		echo d1 >y/d &&
		git add y/d &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		mkdir -p y/d &&
		echo e >y/d/e &&
		echo d2 >z/d &&
		echo f >z/f &&
		git add y/d/e z/d z/f &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '5d-check: Directory/file/file conflict due to directory rename' '
	(
		cd 5d &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (file/directory).*y/d" out &&

		git ls-files -s >out &&
		test_line_count = 6 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 2 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :0:z/d :0:y/f :2:y/d :0:y/d/e &&
		git rev-parse >expect \
			 O:z/b  O:z/c  B:z/d  B:z/f  A:y/d  B:y/d/e &&
		test_cmp expect actual &&

		git hash-object y/d~HEAD >actual &&
		git rev-parse A:y/d >expect &&
		test_cmp expect actual
	)
'

###########################################################################
# Rules suggested by section 5:
#
#   If a subset of to-be-renamed files have a file or directory in the way,
#   "turn off" the directory rename for those specific sub-paths, falling
#   back to old handling.  But, sadly, see testcases 8a and 8b.
###########################################################################


###########################################################################
# SECTION 6: Same side of the merge was the one that did the rename
#
# It may sound obvious that you only want to apply implicit directory
# renames to directories if the _other_ side of history did the renaming.
# If you did make an implementation that didn't explicitly enforce this
# rule, the majority of cases that would fall under this section would
# also be solved by following the rules from the above sections.  But
# there are still a few that stick out, so this section covers them just
# to make sure we also get them right.
###########################################################################

# Testcase 6a, Tricky rename/delete
#   Commit O: z/{b,c,d}
#   Commit A: z/b
#   Commit B: y/{b,c}, z/d
#   Expected: y/b, CONFLICT(rename/delete, z/c -> y/c vs. NULL)
#   Note: We're just checking here that the rename of z/b and z/c to put
#         them under y/ doesn't accidentally catch z/d and make it look like
#         it is also involved in a rename/delete conflict.

test_expect_success '6a-setup: Tricky rename/delete' '
	test_create_repo 6a &&
	(
		cd 6a &&

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
		git rm z/c &&
		git rm z/d &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		mkdir y &&
		git mv z/b y/ &&
		git mv z/c y/ &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '6a-check: Tricky rename/delete' '
	(
		cd 6a &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (rename/delete).*z/c.*y/c" out &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :3:y/c &&
		git rev-parse >expect \
			 O:z/b  O:z/c &&
		test_cmp expect actual
	)
'

# Testcase 6b, Same rename done on both sides
#   (Related to testcases 6c and 8e)
#   Commit O: z/{b,c}
#   Commit A: y/{b,c}
#   Commit B: y/{b,c}, z/d
#   Expected: y/{b,c}, z/d
#   Note: If we did directory rename detection here, we'd move z/d into y/,
#         but B did that rename and still decided to put the file into z/,
#         so we probably shouldn't apply directory rename detection for it.

test_expect_success '6b-setup: Same rename done on both sides' '
	test_create_repo 6b &&
	(
		cd 6b &&

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
		git mv z y &&
		mkdir z &&
		echo d >z/d &&
		git add z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '6b-check: Same rename done on both sides' '
	(
		cd 6b &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:z/d &&
		git rev-parse >expect \
			O:z/b    O:z/c    B:z/d &&
		test_cmp expect actual
	)
'

# Testcase 6c, Rename only done on same side
#   (Related to testcases 6b and 8e)
#   Commit O: z/{b,c}
#   Commit A: z/{b,c} (no change)
#   Commit B: y/{b,c}, z/d
#   Expected: y/{b,c}, z/d
#   NOTE: Seems obvious, but just checking that the implementation doesn't
#         "accidentally detect a rename" and give us y/{b,c,d}.

test_expect_success '6c-setup: Rename only done on same side' '
	test_create_repo 6c &&
	(
		cd 6c &&

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
		test_tick &&
		git commit --allow-empty -m "A" &&

		git checkout B &&
		git mv z y &&
		mkdir z &&
		echo d >z/d &&
		git add z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '6c-check: Rename only done on same side' '
	(
		cd 6c &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:z/d &&
		git rev-parse >expect \
			O:z/b    O:z/c    B:z/d &&
		test_cmp expect actual
	)
'

# Testcase 6d, We don't always want transitive renaming
#   (Related to testcase 1c)
#   Commit O: z/{b,c}, x/d
#   Commit A: z/{b,c}, x/d (no change)
#   Commit B: y/{b,c}, z/d
#   Expected: y/{b,c}, z/d
#   NOTE: Again, this seems obvious but just checking that the implementation
#         doesn't "accidentally detect a rename" and give us y/{b,c,d}.

test_expect_success '6d-setup: We do not always want transitive renaming' '
	test_create_repo 6d &&
	(
		cd 6d &&

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
		test_tick &&
		git commit --allow-empty -m "A" &&

		git checkout B &&
		git mv z y &&
		git mv x z &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '6d-check: We do not always want transitive renaming' '
	(
		cd 6d &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:z/d &&
		git rev-parse >expect \
			O:z/b    O:z/c    O:x/d &&
		test_cmp expect actual
	)
'

# Testcase 6e, Add/add from one-side
#   Commit O: z/{b,c}
#   Commit A: z/{b,c} (no change)
#   Commit B: y/{b,c,d_1}, z/d_2
#   Expected: y/{b,c,d_1}, z/d_2
#   NOTE: Again, this seems obvious but just checking that the implementation
#         doesn't "accidentally detect a rename" and give us y/{b,c} +
#         add/add conflict on y/d_1 vs y/d_2.

test_expect_success '6e-setup: Add/add from one side' '
	test_create_repo 6e &&
	(
		cd 6e &&

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
		test_tick &&
		git commit --allow-empty -m "A" &&

		git checkout B &&
		git mv z y &&
		echo d1 > y/d &&
		mkdir z &&
		echo d2 > z/d &&
		git add y/d z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '6e-check: Add/add from one side' '
	(
		cd 6e &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/d HEAD:z/d &&
		git rev-parse >expect \
			O:z/b    O:z/c    B:y/d    B:z/d &&
		test_cmp expect actual
	)
'

###########################################################################
# Rules suggested by section 6:
#
#   Only apply implicit directory renames to directories if the other
#   side of history is the one doing the renaming.
###########################################################################


###########################################################################
# SECTION 7: More involved Edge/Corner cases
#
# The ruleset we have generated in the above sections seems to provide
# well-defined merges.  But can we find edge/corner cases that either (a)
# are harder for users to understand, or (b) have a resolution that is
# non-intuitive or suboptimal?
#
# The testcases in this section dive into cases that I've tried to craft in
# a way to find some that might be surprising to users or difficult for
# them to understand (the next section will look at non-intuitive or
# suboptimal merge results).  Some of the testcases are similar to ones
# from past sections, but have been simplified to try to highlight error
# messages using a "modified" path (due to the directory rename).  Are
# users okay with these?
#
# In my opinion, testcases that are difficult to understand from this
# section is due to difficulty in the testcase rather than the directory
# renaming (similar to how t6042 and t6036 have difficult resolutions due
# to the problem setup itself being complex).  And I don't think the
# error messages are a problem.
#
# On the other hand, the testcases in section 8 worry me slightly more...
###########################################################################

# Testcase 7a, rename-dir vs. rename-dir (NOT split evenly) PLUS add-other-file
#   Commit O: z/{b,c}
#   Commit A: y/{b,c}
#   Commit B: w/b, x/c, z/d
#   Expected: y/d, CONFLICT(rename/rename for both z/b and z/c)
#   NOTE: There's a rename of z/ here, y/ has more renames, so z/d -> y/d.

test_expect_success '7a-setup: rename-dir vs. rename-dir (NOT split evenly) PLUS add-other-file' '
	test_create_repo 7a &&
	(
		cd 7a &&

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
		mkdir w &&
		mkdir x &&
		git mv z/b w/ &&
		git mv z/c x/ &&
		echo d > z/d &&
		git add z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '7a-check: rename-dir vs. rename-dir (NOT split evenly) PLUS add-other-file' '
	(
		cd 7a &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (rename/rename).*z/b.*y/b.*w/b" out &&
		test_i18ngrep "CONFLICT (rename/rename).*z/c.*y/c.*x/c" out &&

		git ls-files -s >out &&
		test_line_count = 7 out &&
		git ls-files -u >out &&
		test_line_count = 6 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:1:z/b :2:y/b :3:w/b :1:z/c :2:y/c :3:x/c :0:y/d &&
		git rev-parse >expect \
			 O:z/b  O:z/b  O:z/b  O:z/c  O:z/c  O:z/c  B:z/d &&
		test_cmp expect actual &&

		git hash-object >actual \
			y/b   w/b   y/c   x/c &&
		git rev-parse >expect \
			O:z/b O:z/b O:z/c O:z/c &&
		test_cmp expect actual
	)
'

# Testcase 7b, rename/rename(2to1), but only due to transitive rename
#   (Related to testcase 1d)
#   Commit O: z/{b,c},     x/d_1, w/d_2
#   Commit A: y/{b,c,d_2}, x/d_1
#   Commit B: z/{b,c,d_1},        w/d_2
#   Expected: y/{b,c}, CONFLICT(rename/rename(2to1): x/d_1, w/d_2 -> y_d)

test_expect_success '7b-setup: rename/rename(2to1), but only due to transitive rename' '
	test_create_repo 7b &&
	(
		cd 7b &&

		mkdir z &&
		mkdir x &&
		mkdir w &&
		echo b >z/b &&
		echo c >z/c &&
		echo d1 > x/d &&
		echo d2 > w/d &&
		git add z x w &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z y &&
		git mv w/d y/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/d z/ &&
		rmdir x &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '7b-check: rename/rename(2to1), but only due to transitive rename' '
	(
		cd 7b &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (rename/rename)" out &&

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :2:y/d :3:y/d &&
		git rev-parse >expect \
			 O:z/b  O:z/c  O:w/d  O:x/d &&
		test_cmp expect actual &&

		test_path_is_missing y/d &&
		test_path_is_file y/d~HEAD &&
		test_path_is_file y/d~B^0 &&

		git hash-object >actual \
			y/d~HEAD y/d~B^0 &&
		git rev-parse >expect \
			O:w/d    O:x/d &&
		test_cmp expect actual
	)
'

# Testcase 7c, rename/rename(1to...2or3); transitive rename may add complexity
#   (Related to testcases 3b and 5c)
#   Commit O: z/{b,c}, x/d
#   Commit A: y/{b,c}, w/d
#   Commit B: z/{b,c,d}
#   Expected: y/{b,c}, CONFLICT(x/d -> w/d vs. y/d)
#   NOTE: z/ was renamed to y/ so we do want to report
#         neither CONFLICT(x/d -> w/d vs. z/d)
#         nor CONFLiCT x/d -> w/d vs. y/d vs. z/d)

test_expect_success '7c-setup: rename/rename(1to...2or3); transitive rename may add complexity' '
	test_create_repo 7c &&
	(
		cd 7c &&

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
		git mv x w &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/d z/ &&
		rmdir x &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '7c-check: rename/rename(1to...2or3); transitive rename may add complexity' '
	(
		cd 7c &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (rename/rename).*x/d.*w/d.*y/d" out &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :1:x/d :2:w/d :3:y/d &&
		git rev-parse >expect \
			 O:z/b  O:z/c  O:x/d  O:x/d  O:x/d &&
		test_cmp expect actual
	)
'

# Testcase 7d, transitive rename involved in rename/delete; how is it reported?
#   (Related somewhat to testcases 5b and 8d)
#   Commit O: z/{b,c}, x/d
#   Commit A: y/{b,c}
#   Commit B: z/{b,c,d}
#   Expected: y/{b,c}, CONFLICT(delete x/d vs rename to y/d)
#   NOTE: z->y so NOT CONFLICT(delete x/d vs rename to z/d)

test_expect_success '7d-setup: transitive rename involved in rename/delete; how is it reported?' '
	test_create_repo 7d &&
	(
		cd 7d &&

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
		git rm -rf x &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/d z/ &&
		rmdir x &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '7d-check: transitive rename involved in rename/delete; how is it reported?' '
	(
		cd 7d &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (rename/delete).*x/d.*y/d" out &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :3:y/d &&
		git rev-parse >expect \
			 O:z/b  O:z/c  O:x/d &&
		test_cmp expect actual
	)
'

# Testcase 7e, transitive rename in rename/delete AND dirs in the way
#   (Very similar to 'both rename source and destination involved in D/F conflict' from t6022-merge-rename.sh)
#   (Also related to testcases 9c and 9d)
#   Commit O: z/{b,c},     x/d_1
#   Commit A: y/{b,c,d/g}, x/d/f
#   Commit B: z/{b,c,d_1}
#   Expected: rename/delete(x/d_1->y/d_1 vs. None) + D/F conflict on y/d
#             y/{b,c,d/g}, y/d_1~B^0, x/d/f

#   NOTE: The main path of interest here is d_1 and where it ends up, but
#         this is actually a case that has two potential directory renames
#         involved and D/F conflict(s), so it makes sense to walk through
#         each step.
#
#         Commit A renames z/ -> y/.  Thus everything that B adds to z/
#         should be instead moved to y/.  This gives us the D/F conflict on
#         y/d because x/d_1 -> z/d_1 -> y/d_1 conflicts with y/d/g.
#
#         Further, commit B renames x/ -> z/, thus everything A adds to x/
#         should instead be moved to z/...BUT we removed z/ and renamed it
#         to y/, so maybe everything should move not from x/ to z/, but
#         from x/ to z/ to y/.  Doing so might make sense from the logic so
#         far, but note that commit A had both an x/ and a y/; it did the
#         renaming of z/ to y/ and created x/d/f and it clearly made these
#         things separate, so it doesn't make much sense to push these
#         together.  Doing so is what I'd call a doubly transitive rename;
#         see testcases 9c and 9d for further discussion of this issue and
#         how it's resolved.

test_expect_success '7e-setup: transitive rename in rename/delete AND dirs in the way' '
	test_create_repo 7e &&
	(
		cd 7e &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		mkdir x &&
		echo d1 >x/d &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z y &&
		git rm x/d &&
		mkdir -p x/d &&
		mkdir -p y/d &&
		echo f >x/d/f &&
		echo g >y/d/g &&
		git add x/d/f y/d/g &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/d z/ &&
		rmdir x &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '7e-check: transitive rename in rename/delete AND dirs in the way' '
	(
		cd 7e &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (rename/delete).*x/d.*y/d" out &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 2 out &&

		git rev-parse >actual \
			:0:x/d/f :0:y/d/g :0:y/b :0:y/c :3:y/d &&
		git rev-parse >expect \
			 A:x/d/f  A:y/d/g  O:z/b  O:z/c  O:x/d &&
		test_cmp expect actual &&

		git hash-object y/d~B^0 >actual &&
		git rev-parse O:x/d >expect &&
		test_cmp expect actual
	)
'

###########################################################################
# SECTION 8: Suboptimal merges
#
# As alluded to in the last section, the ruleset we have built up for
# detecting directory renames unfortunately has some special cases where it
# results in slightly suboptimal or non-intuitive behavior.  This section
# explores these cases.
#
# To be fair, we already had non-intuitive or suboptimal behavior for most
# of these cases in git before introducing implicit directory rename
# detection, but it'd be nice if there was a modified ruleset out there
# that handled these cases a bit better.
###########################################################################

# Testcase 8a, Dual-directory rename, one into the others' way
#   Commit O. x/{a,b},   y/{c,d}
#   Commit A. x/{a,b,e}, y/{c,d,f}
#   Commit B. y/{a,b},   z/{c,d}
#
# Possible Resolutions:
#   w/o dir-rename detection: y/{a,b,f},   z/{c,d},   x/e
#   Currently expected:       y/{a,b,e,f}, z/{c,d}
#   Optimal:                  y/{a,b,e},   z/{c,d,f}
#
# Note: Both x and y got renamed and it'd be nice to detect both, and we do
# better with directory rename detection than git did without, but the
# simple rule from section 5 prevents me from handling this as optimally as
# we potentially could.

test_expect_success '8a-setup: Dual-directory rename, one into the others way' '
	test_create_repo 8a &&
	(
		cd 8a &&

		mkdir x &&
		mkdir y &&
		echo a >x/a &&
		echo b >x/b &&
		echo c >y/c &&
		echo d >y/d &&
		git add x y &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		echo e >x/e &&
		echo f >y/f &&
		git add x/e y/f &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv y z &&
		git mv x y &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '8a-check: Dual-directory rename, one into the others way' '
	(
		cd 8a &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 6 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/a HEAD:y/b HEAD:y/e HEAD:y/f HEAD:z/c HEAD:z/d &&
		git rev-parse >expect \
			O:x/a    O:x/b    A:x/e    A:y/f    O:y/c    O:y/d &&
		test_cmp expect actual
	)
'

# Testcase 8b, Dual-directory rename, one into the others' way, with conflicting filenames
#   Commit O. x/{a_1,b_1},     y/{a_2,b_2}
#   Commit A. x/{a_1,b_1,e_1}, y/{a_2,b_2,e_2}
#   Commit B. y/{a_1,b_1},     z/{a_2,b_2}
#
#   w/o dir-rename detection: y/{a_1,b_1,e_2}, z/{a_2,b_2}, x/e_1
#   Currently expected:       <same>
#   Scary:                    y/{a_1,b_1},     z/{a_2,b_2}, CONFLICT(add/add, e_1 vs. e_2)
#   Optimal:                  y/{a_1,b_1,e_1}, z/{a_2,b_2,e_2}
#
# Note: Very similar to 8a, except instead of 'e' and 'f' in directories x and
# y, both are named 'e'.  Without directory rename detection, neither file
# moves directories.  Implement directory rename detection suboptimally, and
# you get an add/add conflict, but both files were added in commit A, so this
# is an add/add conflict where one side of history added both files --
# something we can't represent in the index.  Obviously, we'd prefer the last
# resolution, but our previous rules are too coarse to allow it.  Using both
# the rules from section 4 and section 5 save us from the Scary resolution,
# making us fall back to pre-directory-rename-detection behavior for both
# e_1 and e_2.

test_expect_success '8b-setup: Dual-directory rename, one into the others way, with conflicting filenames' '
	test_create_repo 8b &&
	(
		cd 8b &&

		mkdir x &&
		mkdir y &&
		echo a1 >x/a &&
		echo b1 >x/b &&
		echo a2 >y/a &&
		echo b2 >y/b &&
		git add x y &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		echo e1 >x/e &&
		echo e2 >y/e &&
		git add x/e y/e &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv y z &&
		git mv x y &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '8b-check: Dual-directory rename, one into the others way, with conflicting filenames' '
	(
		cd 8b &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 6 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/a HEAD:y/b HEAD:z/a HEAD:z/b HEAD:x/e HEAD:y/e &&
		git rev-parse >expect \
			O:x/a    O:x/b    O:y/a    O:y/b    A:x/e    A:y/e &&
		test_cmp expect actual
	)
'

# Testcase 8c, modify/delete or rename+modify/delete?
#   (Related to testcases 5b, 8d, and 9h)
#   Commit O: z/{b,c,d}
#   Commit A: y/{b,c}
#   Commit B: z/{b,c,d_modified,e}
#   Expected: y/{b,c,e}, CONFLICT(modify/delete: on z/d)
#
#   Note: It could easily be argued that the correct resolution here is
#         y/{b,c,e}, CONFLICT(rename/delete: z/d -> y/d vs deleted)
#         and that the modifed version of d should be present in y/ after
#         the merge, just marked as conflicted.  Indeed, I previously did
#         argue that.  But applying directory renames to the side of
#         history where a file is merely modified results in spurious
#         rename/rename(1to2) conflicts -- see testcase 9h.  See also
#         notes in 8d.

test_expect_success '8c-setup: modify/delete or rename+modify/delete?' '
	test_create_repo 8c &&
	(
		cd 8c &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		test_seq 1 10 >z/d &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git rm z/d &&
		git mv z y &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo 11 >z/d &&
		test_chmod +x z/d &&
		echo e >z/e &&
		git add z/d z/e &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '8c-check: modify/delete or rename+modify/delete' '
	(
		cd 8c &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		test_i18ngrep "CONFLICT (modify/delete).* z/d" out &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :0:y/e :1:z/d :3:z/d &&
		git rev-parse >expect \
			 O:z/b  O:z/c  B:z/e  O:z/d  B:z/d &&
		test_cmp expect actual &&

		test_must_fail git rev-parse :2:z/d &&
		git ls-files -s z/d | grep ^100755 &&
		test_path_is_file z/d &&
		test_path_is_missing y/d
	)
'

# Testcase 8d, rename/delete...or not?
#   (Related to testcase 5b; these may appear slightly inconsistent to users;
#    Also related to testcases 7d and 7e)
#   Commit O: z/{b,c,d}
#   Commit A: y/{b,c}
#   Commit B: z/{b,c,d,e}
#   Expected: y/{b,c,e}
#
#   Note: It would also be somewhat reasonable to resolve this as
#             y/{b,c,e}, CONFLICT(rename/delete: x/d -> y/d or deleted)
#
#   In this case, I'm leaning towards: commit A was the one that deleted z/d
#   and it did the rename of z to y, so the two "conflicts" (rename vs.
#   delete) are both coming from commit A, which is illogical.  Conflicts
#   during merging are supposed to be about opposite sides doing things
#   differently.

test_expect_success '8d-setup: rename/delete...or not?' '
	test_create_repo 8d &&
	(
		cd 8d &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		test_seq 1 10 >z/d &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git rm z/d &&
		git mv z y &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo e >z/e &&
		git add z/e &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '8d-check: rename/delete...or not?' '
	(
		cd 8d &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/e &&
		git rev-parse >expect \
			O:z/b    O:z/c    B:z/e &&
		test_cmp expect actual
	)
'

# Testcase 8e, Both sides rename, one side adds to original directory
#   Commit O: z/{b,c}
#   Commit A: y/{b,c}
#   Commit B: w/{b,c}, z/d
#
# Possible Resolutions:
#   w/o dir-rename detection: z/d, CONFLICT(z/b -> y/b vs. w/b),
#                                  CONFLICT(z/c -> y/c vs. w/c)
#   Currently expected:       y/d, CONFLICT(z/b -> y/b vs. w/b),
#                                  CONFLICT(z/c -> y/c vs. w/c)
#   Optimal:                  ??
#
# Notes: In commit A, directory z got renamed to y.  In commit B, directory z
#        did NOT get renamed; the directory is still present; instead it is
#        considered to have just renamed a subset of paths in directory z
#        elsewhere.  Therefore, the directory rename done in commit A to z/
#        applies to z/d and maps it to y/d.
#
#        It's possible that users would get confused about this, but what
#        should we do instead?  Silently leaving at z/d seems just as bad or
#        maybe even worse.  Perhaps we could print a big warning about z/d
#        and how we're moving to y/d in this case, but when I started thinking
#        about the ramifications of doing that, I didn't know how to rule out
#        that opening other weird edge and corner cases so I just punted.

test_expect_success '8e-setup: Both sides rename, one side adds to original directory' '
	test_create_repo 8e &&
	(
		cd 8e &&

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
		git mv z w &&
		mkdir z &&
		echo d >z/d &&
		git add z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '8e-check: Both sides rename, one side adds to original directory' '
	(
		cd 8e &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep CONFLICT.*rename/rename.*z/c.*y/c.*w/c out &&
		test_i18ngrep CONFLICT.*rename/rename.*z/b.*y/b.*w/b out &&

		git ls-files -s >out &&
		test_line_count = 7 out &&
		git ls-files -u >out &&
		test_line_count = 6 out &&
		git ls-files -o >out &&
		test_line_count = 2 out &&

		git rev-parse >actual \
			:1:z/b :2:y/b :3:w/b :1:z/c :2:y/c :3:w/c :0:y/d &&
		git rev-parse >expect \
			 O:z/b  O:z/b  O:z/b  O:z/c  O:z/c  O:z/c  B:z/d &&
		test_cmp expect actual &&

		git hash-object >actual \
			y/b   w/b   y/c   w/c &&
		git rev-parse >expect \
			O:z/b O:z/b O:z/c O:z/c &&
		test_cmp expect actual &&

		test_path_is_missing z/b &&
		test_path_is_missing z/c
	)
'

###########################################################################
# SECTION 9: Other testcases
#
# This section consists of miscellaneous testcases I thought of during
# the implementation which round out the testing.
###########################################################################

# Testcase 9a, Inner renamed directory within outer renamed directory
#   (Related to testcase 1f)
#   Commit O: z/{b,c,d/{e,f,g}}
#   Commit A: y/{b,c}, x/w/{e,f,g}
#   Commit B: z/{b,c,d/{e,f,g,h},i}
#   Expected: y/{b,c,i}, x/w/{e,f,g,h}
#   NOTE: The only reason this one is interesting is because when a directory
#         is split into multiple other directories, we determine by the weight
#         of which one had the most paths going to it.  A naive implementation
#         of that could take the new file in commit B at z/i to x/w/i or x/i.

test_expect_success '9a-setup: Inner renamed directory within outer renamed directory' '
	test_create_repo 9a &&
	(
		cd 9a &&

		mkdir -p z/d &&
		echo b >z/b &&
		echo c >z/c &&
		echo e >z/d/e &&
		echo f >z/d/f &&
		echo g >z/d/g &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir x &&
		git mv z/d x/w &&
		git mv z y &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo h >z/d/h &&
		echo i >z/i &&
		git add z &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '9a-check: Inner renamed directory within outer renamed directory' '
	(
		cd 9a &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 7 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/i &&
		git rev-parse >expect \
			O:z/b    O:z/c    B:z/i &&
		test_cmp expect actual &&

		git rev-parse >actual \
			HEAD:x/w/e HEAD:x/w/f HEAD:x/w/g HEAD:x/w/h &&
		git rev-parse >expect \
			O:z/d/e    O:z/d/f    O:z/d/g    B:z/d/h &&
		test_cmp expect actual
	)
'

# Testcase 9b, Transitive rename with content merge
#   (Related to testcase 1c)
#   Commit O: z/{b,c},   x/d_1
#   Commit A: y/{b,c},   x/d_2
#   Commit B: z/{b,c,d_3}
#   Expected: y/{b,c,d_merged}

test_expect_success '9b-setup: Transitive rename with content merge' '
	test_create_repo 9b &&
	(
		cd 9b &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		mkdir x &&
		test_seq 1 10 >x/d &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z y &&
		test_seq 1 11 >x/d &&
		git add x/d &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		test_seq 0 10 >x/d &&
		git mv x/d z/d &&
		git add z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '9b-check: Transitive rename with content merge' '
	(
		cd 9b &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&

		test_seq 0 11 >expected &&
		test_cmp expected y/d &&
		git add expected &&
		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/d &&
		git rev-parse >expect \
			O:z/b    O:z/c    :0:expected &&
		test_cmp expect actual &&
		test_must_fail git rev-parse HEAD:x/d &&
		test_must_fail git rev-parse HEAD:z/d &&
		test_path_is_missing z/d &&

		test $(git rev-parse HEAD:y/d) != $(git rev-parse O:x/d) &&
		test $(git rev-parse HEAD:y/d) != $(git rev-parse A:x/d) &&
		test $(git rev-parse HEAD:y/d) != $(git rev-parse B:z/d)
	)
'

# Testcase 9c, Doubly transitive rename?
#   (Related to testcase 1c, 7e, and 9d)
#   Commit O: z/{b,c},     x/{d,e},    w/f
#   Commit A: y/{b,c},     x/{d,e,f,g}
#   Commit B: z/{b,c,d,e},             w/f
#   Expected: y/{b,c,d,e}, x/{f,g}
#
#   NOTE: x/f and x/g may be slightly confusing here.  The rename from w/f to
#         x/f is clear.  Let's look beyond that.  Here's the logic:
#            Commit B renamed x/ -> z/
#            Commit A renamed z/ -> y/
#         So, we could possibly further rename x/f to z/f to y/f, a doubly
#         transient rename.  However, where does it end?  We can chain these
#         indefinitely (see testcase 9d).  What if there is a D/F conflict
#         at z/f/ or y/f/?  Or just another file conflict at one of those
#         paths?  In the case of an N-long chain of transient renamings,
#         where do we "abort" the rename at?  Can the user make sense of
#         the resulting conflict and resolve it?
#
#         To avoid this confusion I use the simple rule that if the other side
#         of history did a directory rename to a path that your side renamed
#         away, then ignore that particular rename from the other side of
#         history for any implicit directory renames.

test_expect_success '9c-setup: Doubly transitive rename?' '
	test_create_repo 9c &&
	(
		cd 9c &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		mkdir x &&
		echo d >x/d &&
		echo e >x/e &&
		mkdir w &&
		echo f >w/f &&
		git add z x w &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z y &&
		git mv w/f x/ &&
		echo g >x/g &&
		git add x/g &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/d z/d &&
		git mv x/e z/e &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '9c-check: Doubly transitive rename?' '
	(
		cd 9c &&

		git checkout A^0 &&

		git merge -s recursive B^0 >out &&
		test_i18ngrep "WARNING: Avoiding applying x -> z rename to x/f" out &&

		git ls-files -s >out &&
		test_line_count = 6 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/d HEAD:y/e HEAD:x/f HEAD:x/g &&
		git rev-parse >expect \
			O:z/b    O:z/c    O:x/d    O:x/e    O:w/f    A:x/g &&
		test_cmp expect actual
	)
'

# Testcase 9d, N-fold transitive rename?
#   (Related to testcase 9c...and 1c and 7e)
#   Commit O: z/a, y/b, x/c, w/d, v/e, u/f
#   Commit A:  y/{a,b},  w/{c,d},  u/{e,f}
#   Commit B: z/{a,t}, x/{b,c}, v/{d,e}, u/f
#   Expected: <see NOTE first>
#
#   NOTE: z/ -> y/ (in commit A)
#         y/ -> x/ (in commit B)
#         x/ -> w/ (in commit A)
#         w/ -> v/ (in commit B)
#         v/ -> u/ (in commit A)
#         So, if we add a file to z, say z/t, where should it end up?  In u?
#         What if there's another file or directory named 't' in one of the
#         intervening directories and/or in u itself?  Also, shouldn't the
#         same logic that places 't' in u/ also move ALL other files to u/?
#         What if there are file or directory conflicts in any of them?  If
#         we attempted to do N-way (N-fold? N-ary? N-uple?) transitive renames
#         like this, would the user have any hope of understanding any
#         conflicts or how their working tree ended up?  I think not, so I'm
#         ruling out N-ary transitive renames for N>1.
#
#   Therefore our expected result is:
#     z/t, y/a, x/b, w/c, u/d, u/e, u/f
#   The reason that v/d DOES get transitively renamed to u/d is that u/ isn't
#   renamed somewhere.  A slightly sub-optimal result, but it uses fairly
#   simple rules that are consistent with what we need for all the other
#   testcases and simplifies things for the user.

test_expect_success '9d-setup: N-way transitive rename?' '
	test_create_repo 9d &&
	(
		cd 9d &&

		mkdir z y x w v u &&
		echo a >z/a &&
		echo b >y/b &&
		echo c >x/c &&
		echo d >w/d &&
		echo e >v/e &&
		echo f >u/f &&
		git add z y x w v u &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z/a y/ &&
		git mv x/c w/ &&
		git mv v/e u/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo t >z/t &&
		git mv y/b x/ &&
		git mv w/d v/ &&
		git add z/t &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '9d-check: N-way transitive rename?' '
	(
		cd 9d &&

		git checkout A^0 &&

		git merge -s recursive B^0 >out &&
		test_i18ngrep "WARNING: Avoiding applying z -> y rename to z/t" out &&
		test_i18ngrep "WARNING: Avoiding applying y -> x rename to y/a" out &&
		test_i18ngrep "WARNING: Avoiding applying x -> w rename to x/b" out &&
		test_i18ngrep "WARNING: Avoiding applying w -> v rename to w/c" out &&

		git ls-files -s >out &&
		test_line_count = 7 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:z/t \
			HEAD:y/a HEAD:x/b HEAD:w/c \
			HEAD:u/d HEAD:u/e HEAD:u/f &&
		git rev-parse >expect \
			B:z/t    \
			O:z/a    O:y/b    O:x/c    \
			O:w/d    O:v/e    A:u/f &&
		test_cmp expect actual
	)
'

# Testcase 9e, N-to-1 whammo
#   (Related to testcase 9c...and 1c and 7e)
#   Commit O: dir1/{a,b}, dir2/{d,e}, dir3/{g,h}, dirN/{j,k}
#   Commit A: dir1/{a,b,c,yo}, dir2/{d,e,f,yo}, dir3/{g,h,i,yo}, dirN/{j,k,l,yo}
#   Commit B: combined/{a,b,d,e,g,h,j,k}
#   Expected: combined/{a,b,c,d,e,f,g,h,i,j,k,l}, CONFLICT(Nto1) warnings,
#             dir1/yo, dir2/yo, dir3/yo, dirN/yo

test_expect_success '9e-setup: N-to-1 whammo' '
	test_create_repo 9e &&
	(
		cd 9e &&

		mkdir dir1 dir2 dir3 dirN &&
		echo a >dir1/a &&
		echo b >dir1/b &&
		echo d >dir2/d &&
		echo e >dir2/e &&
		echo g >dir3/g &&
		echo h >dir3/h &&
		echo j >dirN/j &&
		echo k >dirN/k &&
		git add dir* &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		echo c  >dir1/c &&
		echo yo >dir1/yo &&
		echo f  >dir2/f &&
		echo yo >dir2/yo &&
		echo i  >dir3/i &&
		echo yo >dir3/yo &&
		echo l  >dirN/l &&
		echo yo >dirN/yo &&
		git add dir* &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv dir1 combined &&
		git mv dir2/* combined/ &&
		git mv dir3/* combined/ &&
		git mv dirN/* combined/ &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success C_LOCALE_OUTPUT '9e-check: N-to-1 whammo' '
	(
		cd 9e &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out &&
		grep "CONFLICT (implicit dir rename): Cannot map more than one path to combined/yo" out >error_line &&
		grep -q dir1/yo error_line &&
		grep -q dir2/yo error_line &&
		grep -q dir3/yo error_line &&
		grep -q dirN/yo error_line &&

		git ls-files -s >out &&
		test_line_count = 16 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 2 out &&

		git rev-parse >actual \
			:0:combined/a :0:combined/b :0:combined/c \
			:0:combined/d :0:combined/e :0:combined/f \
			:0:combined/g :0:combined/h :0:combined/i \
			:0:combined/j :0:combined/k :0:combined/l &&
		git rev-parse >expect \
			 O:dir1/a      O:dir1/b      A:dir1/c \
			 O:dir2/d      O:dir2/e      A:dir2/f \
			 O:dir3/g      O:dir3/h      A:dir3/i \
			 O:dirN/j      O:dirN/k      A:dirN/l &&
		test_cmp expect actual &&

		git rev-parse >actual \
			:0:dir1/yo :0:dir2/yo :0:dir3/yo :0:dirN/yo &&
		git rev-parse >expect \
			 A:dir1/yo  A:dir2/yo  A:dir3/yo  A:dirN/yo &&
		test_cmp expect actual
	)
'

# Testcase 9f, Renamed directory that only contained immediate subdirs
#   (Related to testcases 1e & 9g)
#   Commit O: goal/{a,b}/$more_files
#   Commit A: priority/{a,b}/$more_files
#   Commit B: goal/{a,b}/$more_files, goal/c
#   Expected: priority/{a,b}/$more_files, priority/c

test_expect_success '9f-setup: Renamed directory that only contained immediate subdirs' '
	test_create_repo 9f &&
	(
		cd 9f &&

		mkdir -p goal/a &&
		mkdir -p goal/b &&
		echo foo >goal/a/foo &&
		echo bar >goal/b/bar &&
		echo baz >goal/b/baz &&
		git add goal &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv goal/ priority &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo c >goal/c &&
		git add goal/c &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '9f-check: Renamed directory that only contained immediate subdirs' '
	(
		cd 9f &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			HEAD:priority/a/foo \
			HEAD:priority/b/bar \
			HEAD:priority/b/baz \
			HEAD:priority/c &&
		git rev-parse >expect \
			O:goal/a/foo \
			O:goal/b/bar \
			O:goal/b/baz \
			B:goal/c &&
		test_cmp expect actual &&
		test_must_fail git rev-parse HEAD:goal/c
	)
'

# Testcase 9g, Renamed directory that only contained immediate subdirs, immediate subdirs renamed
#   (Related to testcases 1e & 9f)
#   Commit O: goal/{a,b}/$more_files
#   Commit A: priority/{alpha,bravo}/$more_files
#   Commit B: goal/{a,b}/$more_files, goal/c
#   Expected: priority/{alpha,bravo}/$more_files, priority/c

test_expect_success '9g-setup: Renamed directory that only contained immediate subdirs, immediate subdirs renamed' '
	test_create_repo 9g &&
	(
		cd 9g &&

		mkdir -p goal/a &&
		mkdir -p goal/b &&
		echo foo >goal/a/foo &&
		echo bar >goal/b/bar &&
		echo baz >goal/b/baz &&
		git add goal &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir priority &&
		git mv goal/a/ priority/alpha &&
		git mv goal/b/ priority/beta &&
		rmdir goal/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo c >goal/c &&
		git add goal/c &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_failure '9g-check: Renamed directory that only contained immediate subdirs, immediate subdirs renamed' '
	(
		cd 9g &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			HEAD:priority/alpha/foo \
			HEAD:priority/beta/bar  \
			HEAD:priority/beta/baz  \
			HEAD:priority/c &&
		git rev-parse >expect \
			O:goal/a/foo \
			O:goal/b/bar \
			O:goal/b/baz \
			B:goal/c &&
		test_cmp expect actual &&
		test_must_fail git rev-parse HEAD:goal/c
	)
'

# Testcase 9h, Avoid implicit rename if involved as source on other side
#   (Extremely closely related to testcase 3a)
#   Commit O: z/{b,c,d_1}
#   Commit A: z/{b,c,d_2}
#   Commit B: y/{b,c}, x/d_1
#   Expected: y/{b,c}, x/d_2
#   NOTE: If we applied the z/ -> y/ rename to z/d, then we'd end up with
#         a rename/rename(1to2) conflict (z/d -> y/d vs. x/d)
test_expect_success '9h-setup: Avoid dir rename on merely modified path' '
	test_create_repo 9h &&
	(
		cd 9h &&

		mkdir z &&
		echo b >z/b &&
		echo c >z/c &&
		printf "1\n2\n3\n4\n5\n6\n7\n8\nd\n" >z/d &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_tick &&
		echo more >>z/d &&
		git add z/d &&
		git commit -m "A" &&

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

test_expect_success '9h-check: Avoid dir rename on merely modified path' '
	(
		cd 9h &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:x/d &&
		git rev-parse >expect \
			O:z/b    O:z/c    A:z/d &&
		test_cmp expect actual
	)
'

###########################################################################
# Rules suggested by section 9:
#
#   If the other side of history did a directory rename to a path that your
#   side renamed away, then ignore that particular rename from the other
#   side of history for any implicit directory renames.
###########################################################################

###########################################################################
# SECTION 10: Handling untracked files
#
# unpack_trees(), upon which the recursive merge algorithm is based, aborts
# the operation if untracked or dirty files would be deleted or overwritten
# by the merge.  Unfortunately, unpack_trees() does not understand renames,
# and if it doesn't abort, then it muddies up the working directory before
# we even get to the point of detecting renames, so we need some special
# handling, at least in the case of directory renames.
###########################################################################

# Testcase 10a, Overwrite untracked: normal rename/delete
#   Commit O: z/{b,c_1}
#   Commit A: z/b + untracked z/c + untracked z/d
#   Commit B: z/{b,d_1}
#   Expected: Aborted Merge +
#       ERROR_MSG(untracked working tree files would be overwritten by merge)

test_expect_success '10a-setup: Overwrite untracked with normal rename/delete' '
	test_create_repo 10a &&
	(
		cd 10a &&

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
		git rm z/c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv z/c z/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '10a-check: Overwrite untracked with normal rename/delete' '
	(
		cd 10a &&

		git checkout A^0 &&
		echo very >z/c &&
		echo important >z/d &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "The following untracked working tree files would be overwritten by merge" err &&

		git ls-files -s >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 4 out &&

		echo very >expect &&
		test_cmp expect z/c &&

		echo important >expect &&
		test_cmp expect z/d &&

		git rev-parse HEAD:z/b >actual &&
		git rev-parse O:z/b >expect &&
		test_cmp expect actual
	)
'

# Testcase 10b, Overwrite untracked: dir rename + delete
#   Commit O: z/{b,c_1}
#   Commit A: y/b + untracked y/{c,d,e}
#   Commit B: z/{b,d_1,e}
#   Expected: Failed Merge; y/b + untracked y/c + untracked y/d on disk +
#             z/c_1 -> z/d_1 rename recorded at stage 3 for y/d +
#       ERROR_MSG(refusing to lose untracked file at 'y/d')

test_expect_success '10b-setup: Overwrite untracked with dir rename + delete' '
	test_create_repo 10b &&
	(
		cd 10b &&

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
		git rm z/c &&
		git mv z/ y/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv z/c z/d &&
		echo e >z/e &&
		git add z/e &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '10b-check: Overwrite untracked with dir rename + delete' '
	(
		cd 10b &&

		git checkout A^0 &&
		echo very >y/c &&
		echo important >y/d &&
		echo contents >y/e &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "CONFLICT (rename/delete).*Version B\^0 of y/d left in tree at y/d~B\^0" out &&
		test_i18ngrep "Error: Refusing to lose untracked file at y/e; writing to y/e~B\^0 instead" out &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 5 out &&

		git rev-parse >actual \
			:0:y/b :3:y/d :3:y/e &&
		git rev-parse >expect \
			O:z/b  O:z/c  B:z/e &&
		test_cmp expect actual &&

		echo very >expect &&
		test_cmp expect y/c &&

		echo important >expect &&
		test_cmp expect y/d &&

		echo contents >expect &&
		test_cmp expect y/e
	)
'

# Testcase 10c, Overwrite untracked: dir rename/rename(1to2)
#   Commit O: z/{a,b}, x/{c,d}
#   Commit A: y/{a,b}, w/c, x/d + different untracked y/c
#   Commit B: z/{a,b,c}, x/d
#   Expected: Failed Merge; y/{a,b} + x/d + untracked y/c +
#             CONFLICT(rename/rename) x/c -> w/c vs y/c +
#             y/c~B^0 +
#             ERROR_MSG(Refusing to lose untracked file at y/c)

test_expect_success '10c-setup: Overwrite untracked with dir rename/rename(1to2)' '
	test_create_repo 10c &&
	(
		cd 10c &&

		mkdir z x &&
		echo a >z/a &&
		echo b >z/b &&
		echo c >x/c &&
		echo d >x/d &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir w &&
		git mv x/c w/c &&
		git mv z/ y/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/c z/ &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '10c-check: Overwrite untracked with dir rename/rename(1to2)' '
	(
		cd 10c &&

		git checkout A^0 &&
		echo important >y/c &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "CONFLICT (rename/rename)" out &&
		test_i18ngrep "Refusing to lose untracked file at y/c; adding as y/c~B\^0 instead" out &&

		git ls-files -s >out &&
		test_line_count = 6 out &&
		git ls-files -u >out &&
		test_line_count = 3 out &&
		git ls-files -o >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			:0:y/a :0:y/b :0:x/d :1:x/c :2:w/c :3:y/c &&
		git rev-parse >expect \
			 O:z/a  O:z/b  O:x/d  O:x/c  O:x/c  O:x/c &&
		test_cmp expect actual &&

		git hash-object y/c~B^0 >actual &&
		git rev-parse O:x/c >expect &&
		test_cmp expect actual &&

		echo important >expect &&
		test_cmp expect y/c
	)
'

# Testcase 10d, Delete untracked w/ dir rename/rename(2to1)
#   Commit O: z/{a,b,c_1},        x/{d,e,f_2}
#   Commit A: y/{a,b},            x/{d,e,f_2,wham_1} + untracked y/wham
#   Commit B: z/{a,b,c_1,wham_2}, y/{d,e}
#   Expected: Failed Merge; y/{a,b,d,e} + untracked y/{wham,wham~B^0,wham~HEAD}+
#             CONFLICT(rename/rename) z/c_1 vs x/f_2 -> y/wham
#             ERROR_MSG(Refusing to lose untracked file at y/wham)

test_expect_success '10d-setup: Delete untracked with dir rename/rename(2to1)' '
	test_create_repo 10d &&
	(
		cd 10d &&

		mkdir z x &&
		echo a >z/a &&
		echo b >z/b &&
		echo c >z/c &&
		echo d >x/d &&
		echo e >x/e &&
		echo f >x/f &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z/c x/wham &&
		git mv z/ y/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/f z/wham &&
		git mv x/ y/ &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '10d-check: Delete untracked with dir rename/rename(2to1)' '
	(
		cd 10d &&

		git checkout A^0 &&
		echo important >y/wham &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "CONFLICT (rename/rename)" out &&
		test_i18ngrep "Refusing to lose untracked file at y/wham" out &&

		git ls-files -s >out &&
		test_line_count = 6 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			:0:y/a :0:y/b :0:y/d :0:y/e :2:y/wham :3:y/wham &&
		git rev-parse >expect \
			 O:z/a  O:z/b  O:x/d  O:x/e  O:z/c     O:x/f &&
		test_cmp expect actual &&

		test_must_fail git rev-parse :1:y/wham &&

		echo important >expect &&
		test_cmp expect y/wham &&

		git hash-object >actual \
			y/wham~B^0 y/wham~HEAD &&
		git rev-parse >expect \
			O:x/f      O:z/c &&
		test_cmp expect actual
	)
'

# Testcase 10e, Does git complain about untracked file that's not in the way?
#   Commit O: z/{a,b}
#   Commit A: y/{a,b} + untracked z/c
#   Commit B: z/{a,b,c}
#   Expected: y/{a,b,c} + untracked z/c

test_expect_success '10e-setup: Does git complain about untracked file that is not really in the way?' '
	test_create_repo 10e &&
	(
		cd 10e &&

		mkdir z &&
		echo a >z/a &&
		echo b >z/b &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z/ y/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo c >z/c &&
		git add z/c &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_failure '10e-check: Does git complain about untracked file that is not really in the way?' '
	(
		cd 10e &&

		git checkout A^0 &&
		mkdir z &&
		echo random >z/c &&

		git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep ! "following untracked working tree files would be overwritten by merge" err &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 3 out &&

		git rev-parse >actual \
			:0:y/a :0:y/b :0:y/c &&
		git rev-parse >expect \
			 O:z/a  O:z/b  B:z/c &&
		test_cmp expect actual &&

		echo random >expect &&
		test_cmp expect z/c
	)
'

###########################################################################
# SECTION 11: Handling dirty (not up-to-date) files
#
# unpack_trees(), upon which the recursive merge algorithm is based, aborts
# the operation if untracked or dirty files would be deleted or overwritten
# by the merge.  Unfortunately, unpack_trees() does not understand renames,
# and if it doesn't abort, then it muddies up the working directory before
# we even get to the point of detecting renames, so we need some special
# handling.  This was true even of normal renames, but there are additional
# codepaths that need special handling with directory renames.  Add
# testcases for both renamed-by-directory-rename-detection and standard
# rename cases.
###########################################################################

# Testcase 11a, Avoid losing dirty contents with simple rename
#   Commit O: z/{a,b_v1},
#   Commit A: z/{a,c_v1}, and z/c_v1 has uncommitted mods
#   Commit B: z/{a,b_v2}
#   Expected: ERROR_MSG(Refusing to lose dirty file at z/c) +
#             z/a, staged version of z/c has sha1sum matching B:z/b_v2,
#             z/c~HEAD with contents of B:z/b_v2,
#             z/c with uncommitted mods on top of A:z/c_v1

test_expect_success '11a-setup: Avoid losing dirty contents with simple rename' '
	test_create_repo 11a &&
	(
		cd 11a &&

		mkdir z &&
		echo a >z/a &&
		test_seq 1 10 >z/b &&
		git add z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z/b z/c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo 11 >>z/b &&
		git add z/b &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '11a-check: Avoid losing dirty contents with simple rename' '
	(
		cd 11a &&

		git checkout A^0 &&
		echo stuff >>z/c &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "Refusing to lose dirty file at z/c" out &&

		test_seq 1 10 >expected &&
		echo stuff >>expected &&
		test_cmp expected z/c &&

		git ls-files -s >out &&
		test_line_count = 2 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			:0:z/a :2:z/c &&
		git rev-parse >expect \
			 O:z/a  B:z/b &&
		test_cmp expect actual &&

		git hash-object z/c~HEAD >actual &&
		git rev-parse B:z/b >expect &&
		test_cmp expect actual
	)
'

# Testcase 11b, Avoid losing dirty file involved in directory rename
#   Commit O: z/a,         x/{b,c_v1}
#   Commit A: z/{a,c_v1},  x/b,       and z/c_v1 has uncommitted mods
#   Commit B: y/a,         x/{b,c_v2}
#   Expected: y/{a,c_v2}, x/b, z/c_v1 with uncommitted mods untracked,
#             ERROR_MSG(Refusing to lose dirty file at z/c)


test_expect_success '11b-setup: Avoid losing dirty file involved in directory rename' '
	test_create_repo 11b &&
	(
		cd 11b &&

		mkdir z x &&
		echo a >z/a &&
		echo b >x/b &&
		test_seq 1 10 >x/c &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv x/c z/c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv z y &&
		echo 11 >>x/c &&
		git add x/c &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '11b-check: Avoid losing dirty file involved in directory rename' '
	(
		cd 11b &&

		git checkout A^0 &&
		echo stuff >>z/c &&

		git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "Refusing to lose dirty file at z/c" out &&

		grep -q stuff z/c &&
		test_seq 1 10 >expected &&
		echo stuff >>expected &&
		test_cmp expected z/c &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -m >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			:0:x/b :0:y/a :0:y/c &&
		git rev-parse >expect \
			 O:x/b  O:z/a  B:x/c &&
		test_cmp expect actual &&

		git hash-object y/c >actual &&
		git rev-parse B:x/c >expect &&
		test_cmp expect actual
	)
'

# Testcase 11c, Avoid losing not-up-to-date with rename + D/F conflict
#   Commit O: y/a,         x/{b,c_v1}
#   Commit A: y/{a,c_v1},  x/b,       and y/c_v1 has uncommitted mods
#   Commit B: y/{a,c/d},   x/{b,c_v2}
#   Expected: Abort_msg("following files would be overwritten by merge") +
#             y/c left untouched (still has uncommitted mods)

test_expect_success '11c-setup: Avoid losing not-uptodate with rename + D/F conflict' '
	test_create_repo 11c &&
	(
		cd 11c &&

		mkdir y x &&
		echo a >y/a &&
		echo b >x/b &&
		test_seq 1 10 >x/c &&
		git add y x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv x/c y/c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		mkdir y/c &&
		echo d >y/c/d &&
		echo 11 >>x/c &&
		git add x/c y/c/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '11c-check: Avoid losing not-uptodate with rename + D/F conflict' '
	(
		cd 11c &&

		git checkout A^0 &&
		echo stuff >>y/c &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "following files would be overwritten by merge" err &&

		grep -q stuff y/c &&
		test_seq 1 10 >expected &&
		echo stuff >>expected &&
		test_cmp expected y/c &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -m >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 3 out
	)
'

# Testcase 11d, Avoid losing not-up-to-date with rename + D/F conflict
#   Commit O: z/a,         x/{b,c_v1}
#   Commit A: z/{a,c_v1},  x/b,       and z/c_v1 has uncommitted mods
#   Commit B: y/{a,c/d},   x/{b,c_v2}
#   Expected: D/F: y/c_v2 vs y/c/d) +
#             Warning_Msg("Refusing to lose dirty file at z/c) +
#             y/{a,c~HEAD,c/d}, x/b, now-untracked z/c_v1 with uncommitted mods

test_expect_success '11d-setup: Avoid losing not-uptodate with rename + D/F conflict' '
	test_create_repo 11d &&
	(
		cd 11d &&

		mkdir z x &&
		echo a >z/a &&
		echo b >x/b &&
		test_seq 1 10 >x/c &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv x/c z/c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv z y &&
		mkdir y/c &&
		echo d >y/c/d &&
		echo 11 >>x/c &&
		git add x/c y/c/d &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '11d-check: Avoid losing not-uptodate with rename + D/F conflict' '
	(
		cd 11d &&

		git checkout A^0 &&
		echo stuff >>z/c &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "Refusing to lose dirty file at z/c" out &&

		grep -q stuff z/c &&
		test_seq 1 10 >expected &&
		echo stuff >>expected &&
		test_cmp expected z/c

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		test_line_count = 5 out &&

		git rev-parse >actual \
			:0:x/b :0:y/a :0:y/c/d :3:y/c &&
		git rev-parse >expect \
			 O:x/b  O:z/a  B:y/c/d  B:x/c &&
		test_cmp expect actual &&

		git hash-object y/c~HEAD >actual &&
		git rev-parse B:x/c >expect &&
		test_cmp expect actual
	)
'

# Testcase 11e, Avoid deleting not-up-to-date with dir rename/rename(1to2)/add
#   Commit O: z/{a,b},      x/{c_1,d}
#   Commit A: y/{a,b,c_2},  x/d, w/c_1, and y/c_2 has uncommitted mods
#   Commit B: z/{a,b,c_1},  x/d
#   Expected: Failed Merge; y/{a,b} + x/d +
#             CONFLICT(rename/rename) x/c_1 -> w/c_1 vs y/c_1 +
#             ERROR_MSG(Refusing to lose dirty file at y/c)
#             y/c~B^0 has O:x/c_1 contents
#             y/c~HEAD has A:y/c_2 contents
#             y/c has dirty file from before merge

test_expect_success '11e-setup: Avoid deleting not-uptodate with dir rename/rename(1to2)/add' '
	test_create_repo 11e &&
	(
		cd 11e &&

		mkdir z x &&
		echo a >z/a &&
		echo b >z/b &&
		echo c >x/c &&
		echo d >x/d &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z/ y/ &&
		echo different >y/c &&
		mkdir w &&
		git mv x/c w/ &&
		git add y/c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/c z/ &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '11e-check: Avoid deleting not-uptodate with dir rename/rename(1to2)/add' '
	(
		cd 11e &&

		git checkout A^0 &&
		echo mods >>y/c &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "CONFLICT (rename/rename)" out &&
		test_i18ngrep "Refusing to lose dirty file at y/c" out &&

		git ls-files -s >out &&
		test_line_count = 7 out &&
		git ls-files -u >out &&
		test_line_count = 4 out &&
		git ls-files -o >out &&
		test_line_count = 4 out &&

		echo different >expected &&
		echo mods >>expected &&
		test_cmp expected y/c &&

		git rev-parse >actual \
			:0:y/a :0:y/b :0:x/d :1:x/c :2:w/c :2:y/c :3:y/c &&
		git rev-parse >expect \
			 O:z/a  O:z/b  O:x/d  O:x/c  O:x/c  A:y/c  O:x/c &&
		test_cmp expect actual &&

		git hash-object >actual \
			y/c~B^0 y/c~HEAD &&
		git rev-parse >expect \
			O:x/c   A:y/c &&
		test_cmp expect actual
	)
'

# Testcase 11f, Avoid deleting not-up-to-date w/ dir rename/rename(2to1)
#   Commit O: z/{a,b},        x/{c_1,d_2}
#   Commit A: y/{a,b,wham_1}, x/d_2, except y/wham has uncommitted mods
#   Commit B: z/{a,b,wham_2}, x/c_1
#   Expected: Failed Merge; y/{a,b} + untracked y/{wham~B^0,wham~B^HEAD} +
#             y/wham with dirty changes from before merge +
#             CONFLICT(rename/rename) x/c vs x/d -> y/wham
#             ERROR_MSG(Refusing to lose dirty file at y/wham)

test_expect_success '11f-setup: Avoid deleting not-uptodate with dir rename/rename(2to1)' '
	test_create_repo 11f &&
	(
		cd 11f &&

		mkdir z x &&
		echo a >z/a &&
		echo b >z/b &&
		test_seq 1 10 >x/c &&
		echo d >x/d &&
		git add z x &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z/ y/ &&
		git mv x/c y/wham &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/d z/wham &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '11f-check: Avoid deleting not-uptodate with dir rename/rename(2to1)' '
	(
		cd 11f &&

		git checkout A^0 &&
		echo important >>y/wham &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&
		test_i18ngrep "CONFLICT (rename/rename)" out &&
		test_i18ngrep "Refusing to lose dirty file at y/wham" out &&

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 4 out &&

		test_seq 1 10 >expected &&
		echo important >>expected &&
		test_cmp expected y/wham &&

		test_must_fail git rev-parse :1:y/wham &&
		git hash-object >actual \
			y/wham~B^0 y/wham~HEAD &&
		git rev-parse >expect \
			O:x/d      O:x/c &&
		test_cmp expect actual &&

		git rev-parse >actual \
			:0:y/a :0:y/b :2:y/wham :3:y/wham &&
		git rev-parse >expect \
			 O:z/a  O:z/b  O:x/c     O:x/d &&
		test_cmp expect actual
	)
'

###########################################################################
# SECTION 12: Everything else
#
# Tests suggested by others.  Tests added after implementation completed
# and submitted.  Grab bag.
###########################################################################

# Testcase 12a, Moving one directory hierarchy into another
#   (Related to testcase 9a)
#   Commit O: node1/{leaf1,leaf2}, node2/{leaf3,leaf4}
#   Commit A: node1/{leaf1,leaf2,node2/{leaf3,leaf4}}
#   Commit B: node1/{leaf1,leaf2,leaf5}, node2/{leaf3,leaf4,leaf6}
#   Expected: node1/{leaf1,leaf2,leaf5,node2/{leaf3,leaf4,leaf6}}

test_expect_success '12a-setup: Moving one directory hierarchy into another' '
	test_create_repo 12a &&
	(
		cd 12a &&

		mkdir -p node1 node2 &&
		echo leaf1 >node1/leaf1 &&
		echo leaf2 >node1/leaf2 &&
		echo leaf3 >node2/leaf3 &&
		echo leaf4 >node2/leaf4 &&
		git add node1 node2 &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv node2/ node1/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo leaf5 >node1/leaf5 &&
		echo leaf6 >node2/leaf6 &&
		git add node1 node2 &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '12a-check: Moving one directory hierarchy into another' '
	(
		cd 12a &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 6 out &&

		git rev-parse >actual \
			HEAD:node1/leaf1 HEAD:node1/leaf2 HEAD:node1/leaf5 \
			HEAD:node1/node2/leaf3 \
			HEAD:node1/node2/leaf4 \
			HEAD:node1/node2/leaf6 &&
		git rev-parse >expect \
			O:node1/leaf1    O:node1/leaf2    B:node1/leaf5 \
			O:node2/leaf3 \
			O:node2/leaf4 \
			B:node2/leaf6 &&
		test_cmp expect actual
	)
'

# Testcase 12b, Moving two directory hierarchies into each other
#   (Related to testcases 1c and 12c)
#   Commit O: node1/{leaf1, leaf2}, node2/{leaf3, leaf4}
#   Commit A: node1/{leaf1, leaf2, node2/{leaf3, leaf4}}
#   Commit B: node2/{leaf3, leaf4, node1/{leaf1, leaf2}}
#   Expected: node1/node2/node1/{leaf1, leaf2},
#             node2/node1/node2/{leaf3, leaf4}
#   NOTE: Without directory renames, we would expect
#                   node2/node1/{leaf1, leaf2},
#                   node1/node2/{leaf3, leaf4}
#         with directory rename detection, we note that
#             commit A renames node2/ -> node1/node2/
#             commit B renames node1/ -> node2/node1/
#         therefore, applying those directory renames to the initial result
#         (making all four paths experience a transitive renaming), yields
#         the expected result.
#
#         You may ask, is it weird to have two directories rename each other?
#         To which, I can do no more than shrug my shoulders and say that
#         even simple rules give weird results when given weird inputs.

test_expect_success '12b-setup: Moving one directory hierarchy into another' '
	test_create_repo 12b &&
	(
		cd 12b &&

		mkdir -p node1 node2 &&
		echo leaf1 >node1/leaf1 &&
		echo leaf2 >node1/leaf2 &&
		echo leaf3 >node2/leaf3 &&
		echo leaf4 >node2/leaf4 &&
		git add node1 node2 &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv node2/ node1/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv node1/ node2/ &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '12b-check: Moving one directory hierarchy into another' '
	(
		cd 12b &&

		git checkout A^0 &&

		git merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			HEAD:node1/node2/node1/leaf1 \
			HEAD:node1/node2/node1/leaf2 \
			HEAD:node2/node1/node2/leaf3 \
			HEAD:node2/node1/node2/leaf4 &&
		git rev-parse >expect \
			O:node1/leaf1 \
			O:node1/leaf2 \
			O:node2/leaf3 \
			O:node2/leaf4 &&
		test_cmp expect actual
	)
'

# Testcase 12c, Moving two directory hierarchies into each other w/ content merge
#   (Related to testcase 12b)
#   Commit O: node1/{       leaf1_1, leaf2_1}, node2/{leaf3_1, leaf4_1}
#   Commit A: node1/{       leaf1_2, leaf2_2,  node2/{leaf3_2, leaf4_2}}
#   Commit B: node2/{node1/{leaf1_3, leaf2_3},        leaf3_3, leaf4_3}
#   Expected: Content merge conflicts for each of:
#               node1/node2/node1/{leaf1, leaf2},
#               node2/node1/node2/{leaf3, leaf4}
#   NOTE: This is *exactly* like 12c, except that every path is modified on
#         each side of the merge.

test_expect_success '12c-setup: Moving one directory hierarchy into another w/ content merge' '
	test_create_repo 12c &&
	(
		cd 12c &&

		mkdir -p node1 node2 &&
		printf "1\n2\n3\n4\n5\n6\n7\n8\nleaf1\n" >node1/leaf1 &&
		printf "1\n2\n3\n4\n5\n6\n7\n8\nleaf2\n" >node1/leaf2 &&
		printf "1\n2\n3\n4\n5\n6\n7\n8\nleaf3\n" >node2/leaf3 &&
		printf "1\n2\n3\n4\n5\n6\n7\n8\nleaf4\n" >node2/leaf4 &&
		git add node1 node2 &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv node2/ node1/ &&
		for i in `git ls-files`; do echo side A >>$i; done &&
		git add -u &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv node1/ node2/ &&
		for i in `git ls-files`; do echo side B >>$i; done &&
		git add -u &&
		test_tick &&
		git commit -m "B"
	)
'

test_expect_success '12c-check: Moving one directory hierarchy into another w/ content merge' '
	(
		cd 12c &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 &&

		git ls-files -u >out &&
		test_line_count = 12 out &&

		git rev-parse >actual \
			:1:node1/node2/node1/leaf1 \
			:1:node1/node2/node1/leaf2 \
			:1:node2/node1/node2/leaf3 \
			:1:node2/node1/node2/leaf4 \
			:2:node1/node2/node1/leaf1 \
			:2:node1/node2/node1/leaf2 \
			:2:node2/node1/node2/leaf3 \
			:2:node2/node1/node2/leaf4 \
			:3:node1/node2/node1/leaf1 \
			:3:node1/node2/node1/leaf2 \
			:3:node2/node1/node2/leaf3 \
			:3:node2/node1/node2/leaf4 &&
		git rev-parse >expect \
			O:node1/leaf1 \
			O:node1/leaf2 \
			O:node2/leaf3 \
			O:node2/leaf4 \
			A:node1/leaf1 \
			A:node1/leaf2 \
			A:node1/node2/leaf3 \
			A:node1/node2/leaf4 \
			B:node2/node1/leaf1 \
			B:node2/node1/leaf2 \
			B:node2/leaf3 \
			B:node2/leaf4 &&
		test_cmp expect actual
	)
'

test_done
