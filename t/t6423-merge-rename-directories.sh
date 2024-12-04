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
. "$TEST_DIRECTORY"/lib-merge.sh


###########################################################################
# SECTION 1: Basic cases we should be able to handle
###########################################################################

# Testcase 1a, Basic directory rename.
#   Commit O: z/{b,c}
#   Commit A: y/{b,c}
#   Commit B: z/{b,c,d,e/f}
#   Expected: y/{b,c,d,e/f}

test_setup_1a () {
	git init 1a &&
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
}

test_expect_success '1a: Simple directory rename detection' '
	test_setup_1a &&
	(
		cd 1a &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 >out &&

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

test_setup_1b () {
	git init 1b &&
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
}

test_expect_success '1b: Merge a directory with another' '
	test_setup_1b &&
	(
		cd 1b &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_1c () {
	git init 1c &&
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
}

test_expect_success '1c: Transitive renaming' '
	test_setup_1c &&
	(
		cd 1c &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 >out &&

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

test_setup_1d () {
	git init 1d &&
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
}

test_expect_success '1d: Directory renames cause a rename/rename(2to1) conflict' '
	test_setup_1d &&
	(
		cd 1d &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (\(.*\)/\1)" out &&

		git ls-files -s >out &&
		test_line_count = 8 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

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

		# Test that the two-way merge in x/wham is as expected
		git cat-file -p :2:x/wham >expect &&
		git cat-file -p :3:x/wham >other &&
		>empty &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_must_fail git merge-file \
				-L "HEAD:y/wham" \
				-L "" \
				-L "B^0:z/wham" \
				expect empty other
		else
			test_must_fail git merge-file \
				-L "HEAD" \
				-L "" \
				-L "B^0" \
				expect empty other
		fi &&
		test_cmp expect x/wham
	)
'

# Testcase 1e, Renamed directory, with all filenames being renamed too
#   (Related to testcases 9f & 9g)
#   Commit O: z/{oldb,oldc}
#   Commit A: y/{newb,newc}
#   Commit B: z/{oldb,oldc,d}
#   Expected: y/{newb,newc,d}

test_setup_1e () {
	git init 1e &&
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
}

test_expect_success '1e: Renamed directory, with all files being renamed too' '
	test_setup_1e &&
	(
		cd 1e &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_1f () {
	git init 1f &&
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
}

test_expect_success '1f: Split a directory into two other directories' '
	test_setup_1f &&
	(
		cd 1f &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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
#   with the most renames, "wins" (see 1f).  However, see the testcases
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
test_setup_2a () {
	git init 2a &&
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
}

test_expect_success '2a: Directory split into two on one side, with equal numbers of paths' '
	test_setup_2a &&
	(
		cd 2a &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT.*directory rename split" out &&

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
test_setup_2b () {
	git init 2b &&
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
}

test_expect_success '2b: Directory split into two on one side, with equal numbers of paths' '
	test_setup_2b &&
	(
		cd 2b &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 >out &&

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
		test_grep ! "CONFLICT.*directory rename split" out
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
test_setup_3a () {
	git init 3a &&
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
}

test_expect_success '3a: Avoid implicit rename if involved as source on other side' '
	test_setup_3a &&
	(
		cd 3a &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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
test_setup_3b () {
	git init 3b &&
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
}

test_expect_success '3b: Avoid implicit rename if involved as source on current side' '
	test_setup_3b &&
	(
		cd 3b &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep CONFLICT.*rename/rename.*z/d.*x/d.*w/d out &&
		test_grep ! CONFLICT.*rename/rename.*y/d out &&

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
# equivalently, fully renamed a directory in one commit and then recreated
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

test_setup_4a () {
	git init 4a &&
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
}

test_expect_success '4a: Directory split, with original directory still present' '
	test_setup_4a &&
	(
		cd 4a &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_5a () {
	git init 5a &&
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
}

test_expect_success '5a: Merge directories, other side adds files to original and target' '
	test_setup_5a &&
	(
		cd 5a &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT.*implicit dir rename" out &&

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
#         we normally would since z/ is being renamed to y/, then this would be
#         a rename/delete (z/d_1 -> y/d_1 vs. deleted) AND an add/add/add
#         conflict of y/d_1 vs. y/d_2 vs. y/d_3.  Add/add/add is not
#         representable in the index, so the existence of y/d_3 needs to
#         cause us to bail on directory rename detection for that path, falling
#         back to git behavior without the directory rename detection.

test_setup_5b () {
	git init 5b &&
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
}

test_expect_success '5b: Rename/delete in order to get add/add/add conflict' '
	test_setup_5b &&
	(
		cd 5b &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (add/add).* y/d" out &&

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

test_setup_5c () {
	git init 5c &&
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
}

test_expect_success '5c: Transitive rename would cause rename/rename/rename/add/add/add' '
	test_setup_5c &&
	(
		cd 5c &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (rename/rename).*x/d.*w/d.*z/d" out &&
		test_grep "CONFLICT (add/add).* y/d" out &&

		git ls-files -s >out &&
		test_line_count = 9 out &&
		git ls-files -u >out &&
		test_line_count = 6 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

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
			z/d &&
		git rev-parse >expect \
			O:x/d &&
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

test_setup_5d () {
	git init 5d &&
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
}

test_expect_success '5d: Directory/file/file conflict due to directory rename' '
	test_setup_5d &&
	(
		cd 5d &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (file/directory).*y/d" out &&

		git ls-files -s >out &&
		test_line_count = 6 out &&
		git ls-files -u >out &&
		test_line_count = 1 out &&
		git ls-files -o >out &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_line_count = 1 out &&

			git rev-parse >actual \
			    :0:y/b :0:y/c :0:z/d :0:y/f :2:y/d~HEAD :0:y/d/e
		else
			test_line_count = 2 out &&

			git rev-parse >actual \
			    :0:y/b :0:y/c :0:z/d :0:y/f :2:y/d      :0:y/d/e
		fi &&
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

test_setup_6a () {
	git init 6a &&
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
}

test_expect_success '6a: Tricky rename/delete' '
	test_setup_6a &&
	(
		cd 6a &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (rename/delete).*z/c.*y/c" out &&

		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git ls-files -s >out &&
			test_line_count = 3 out &&
			git ls-files -u >out &&
			test_line_count = 2 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >actual \
				:0:y/b :1:y/c :3:y/c &&
			git rev-parse >expect \
				 O:z/b  O:z/c  O:z/c
		else
			git ls-files -s >out &&
			test_line_count = 2 out &&
			git ls-files -u >out &&
			test_line_count = 1 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >actual \
				:0:y/b :3:y/c &&
			git rev-parse >expect \
				 O:z/b  O:z/c
		fi &&
		test_cmp expect actual
	)
'

# Testcase 6b1, Same rename done on both sides
#   (Related to testcase 6b2 and 8e)
#   Commit O: z/{b,c,d,e}
#   Commit A: y/{b,c,d}, x/e
#   Commit B: y/{b,c,d}, z/{e,f}
#   Expected: y/{b,c,d,f}, x/e
#   Note: Directory rename detection says A renamed z/ -> y/ (3 paths renamed
#         to y/ and only 1 renamed to x/), therefore the new file 'z/f' in B
#         should be moved to 'y/f'.
#
#         This is a bit of an edge case where any behavior might surprise users,
#         whether that is treating A as renaming z/ -> y/, treating A as renaming
#         z/ -> x/, or treating A as not doing any directory rename.  However, I
#         think this answer is the least confusing and most consistent with the
#         rules elsewhere.
#
#         A note about z/ -> x/, since it may not be clear how that could come
#         about: If we were to ignore files renamed by both sides
#         (i.e. z/{b,c,d}), as directory rename detection did in git-2.18 thru
#         at least git-2.28, then we would note there are no renames from z/ to
#         y/ and one rename from z/ to x/ and thus come to the conclusion that
#         A renamed z/ -> x/.  This seems more confusing for end users than a
#         rename of z/ to y/, it makes directory rename detection behavior
#         harder for them to predict.  As such, we modified the rule, changed
#         the behavior on testcases 6b2 and 8e, and introduced this 6b1 testcase.

test_setup_6b1 () {
	git init 6b1 &&
	(
		cd 6b1 &&

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
		git mv z y &&
		mkdir x &&
		git mv y/e x/e &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv z y &&
		mkdir z &&
		git mv y/e z/e &&
		echo f >z/f &&
		git add z/f &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_merge_algorithm failure success '6b1: Same renames done on both sides, plus another rename' '
	test_setup_6b1 &&
	(
		cd 6b1 &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 5 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/d HEAD:x/e HEAD:y/f &&
		git rev-parse >expect \
			O:z/b    O:z/c    O:z/d    O:z/e    B:z/f &&
		test_cmp expect actual
	)
'

# Testcase 6b2, Same rename done on both sides
#   (Related to testcases 6c and 8e)
#   Commit O: z/{b,c}
#   Commit A: y/{b,c}
#   Commit B: y/{b,c}, z/d
#   Expected: y/{b,c,d}
#   Alternate: y/{b,c}, z/d
#   Note: Directory rename detection says A renamed z/ -> y/, therefore the new
#         file 'z/d' in B should be moved to 'y/d'.
#
#         We could potentially ignore the renames of z/{b,c} on side A since
#         those were renamed on both sides.  However, it's a bit of a corner
#         case because what if there was also a z/e that side A moved to x/e
#         and side B left alone?  If we used the "ignore renames done on both
#         sides" logic, then we'd compute that A renamed z/ -> x/, and move
#         z/d to x/d.  That seems more surprising and uglier than allowing
#         the z/ -> y/ rename.

test_setup_6b2 () {
	git init 6b2 &&
	(
		cd 6b2 &&

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
}

test_expect_merge_algorithm failure success '6b2: Same rename done on both sides' '
	test_setup_6b2 &&
	(
		cd 6b2 &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 3 out &&
		git ls-files -u >out &&
		test_line_count = 0 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			HEAD:y/b HEAD:y/c HEAD:y/d &&
		git rev-parse >expect \
			O:z/b    O:z/c    B:z/d &&
		test_cmp expect actual
	)
'

# Testcase 6c, Rename only done on same side
#   (Related to testcases 6b1, 6b2, and 8e)
#   Commit O: z/{b,c}
#   Commit A: z/{b,c} (no change)
#   Commit B: y/{b,c}, z/d
#   Expected: y/{b,c}, z/d
#   NOTE: Seems obvious, but just checking that the implementation doesn't
#         "accidentally detect a rename" and give us y/{b,c,d}.

test_setup_6c () {
	git init 6c &&
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
}

test_expect_success '6c: Rename only done on same side' '
	test_setup_6c &&
	(
		cd 6c &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_6d () {
	git init 6d &&
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
}

test_expect_success '6d: We do not always want transitive renaming' '
	test_setup_6d &&
	(
		cd 6d &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_6e () {
	git init 6e &&
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
}

test_expect_success '6e: Add/add from one side' '
	test_setup_6e &&
	(
		cd 6e &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_7a () {
	git init 7a &&
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
}

test_expect_success '7a: rename-dir vs. rename-dir (NOT split evenly) PLUS add-other-file' '
	test_setup_7a &&
	(
		cd 7a &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (rename/rename).*z/b.*y/b.*w/b" out &&
		test_grep "CONFLICT (rename/rename).*z/c.*y/c.*x/c" out &&

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

test_setup_7b () {
	git init 7b &&
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
}

test_expect_success '7b: rename/rename(2to1), but only due to transitive rename' '
	test_setup_7b &&
	(
		cd 7b &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (\(.*\)/\1)" out &&

		git ls-files -s >out &&
		test_line_count = 4 out &&
		git ls-files -u >out &&
		test_line_count = 2 out &&
		git ls-files -o >out &&
		test_line_count = 1 out &&

		git rev-parse >actual \
			:0:y/b :0:y/c :2:y/d :3:y/d &&
		git rev-parse >expect \
			 O:z/b  O:z/c  O:w/d  O:x/d &&
		test_cmp expect actual &&

		# Test that the two-way merge in y/d is as expected
		git cat-file -p :2:y/d >expect &&
		git cat-file -p :3:y/d >other &&
		>empty &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_must_fail git merge-file \
				-L "HEAD:y/d" \
				-L "" \
				-L "B^0:z/d" \
				expect empty other
		else
			test_must_fail git merge-file \
				-L "HEAD" \
				-L "" \
				-L "B^0" \
				expect empty other
		fi &&
		test_cmp expect y/d
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

test_setup_7c () {
	git init 7c &&
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
}

test_expect_success '7c: rename/rename(1to...2or3); transitive rename may add complexity' '
	test_setup_7c &&
	(
		cd 7c &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (rename/rename).*x/d.*w/d.*y/d" out &&

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

test_setup_7d () {
	git init 7d &&
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
}

test_expect_success '7d: transitive rename involved in rename/delete; how is it reported?' '
	test_setup_7d &&
	(
		cd 7d &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (rename/delete).*x/d.*y/d" out &&

		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git ls-files -s >out &&
			test_line_count = 4 out &&
			git ls-files -u >out &&
			test_line_count = 2 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >actual \
				:0:y/b :0:y/c :1:y/d :3:y/d &&
			git rev-parse >expect \
				 O:z/b  O:z/c  O:x/d  O:x/d
		else
			git ls-files -s >out &&
			test_line_count = 3 out &&
			git ls-files -u >out &&
			test_line_count = 1 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >actual \
				:0:y/b :0:y/c :3:y/d &&
			git rev-parse >expect \
				 O:z/b  O:z/c  O:x/d
		fi &&
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

test_setup_7e () {
	git init 7e &&
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
}

test_expect_success '7e: transitive rename in rename/delete AND dirs in the way' '
	test_setup_7e &&
	(
		cd 7e &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (rename/delete).*x/d.*y/d" out &&

		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			git ls-files -s >out &&
			test_line_count = 6 out &&
			git ls-files -u >out &&
			test_line_count = 2 out &&
			git ls-files -o >out &&
			test_line_count = 1 out &&

			git rev-parse >actual \
				:0:x/d/f :0:y/d/g :0:y/b :0:y/c :1:y/d~B^0 :3:y/d~B^0 &&
			git rev-parse >expect \
				 A:x/d/f  A:y/d/g  O:z/b  O:z/c  O:x/d      O:x/d
		else
			git ls-files -s >out &&
			test_line_count = 5 out &&
			git ls-files -u >out &&
			test_line_count = 1 out &&
			git ls-files -o >out &&
			test_line_count = 2 out &&

			git rev-parse >actual \
				:0:x/d/f :0:y/d/g :0:y/b :0:y/c :3:y/d &&
			git rev-parse >expect \
				 A:x/d/f  A:y/d/g  O:z/b  O:z/c  O:x/d
		fi &&
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

test_setup_8a () {
	git init 8a &&
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
}

test_expect_success '8a: Dual-directory rename, one into the others way' '
	test_setup_8a &&
	(
		cd 8a &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_8b () {
	git init 8b &&
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
}

test_expect_success '8b: Dual-directory rename, one into the others way, with conflicting filenames' '
	test_setup_8b &&
	(
		cd 8b &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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
#         and that the modified version of d should be present in y/ after
#         the merge, just marked as conflicted.  Indeed, I previously did
#         argue that.  But applying directory renames to the side of
#         history where a file is merely modified results in spurious
#         rename/rename(1to2) conflicts -- see testcase 9h.  See also
#         notes in 8d.

test_setup_8c () {
	git init 8c &&
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
}

test_expect_success '8c: modify/delete or rename+modify/delete' '
	test_setup_8c &&
	(
		cd 8c &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "CONFLICT (modify/delete).* z/d" out &&

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

test_setup_8d () {
	git init 8d &&
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
}

test_expect_success '8d: rename/delete...or not?' '
	test_setup_8d &&
	(
		cd 8d &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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
#   if z not considered renamed: z/d, CONFLICT(z/b -> y/b vs. w/b),
#                                     CONFLICT(z/c -> y/c vs. w/c)
#   if z->y rename considered:   y/d, CONFLICT(z/b -> y/b vs. w/b),
#                                     CONFLICT(z/c -> y/c vs. w/c)
#   Optimal:                     ??
#
# Notes: In commit A, directory z got renamed to y.  In commit B, directory z
#        did NOT get renamed; the directory is still present; instead it is
#        considered to have just renamed a subset of paths in directory z
#        elsewhere.  This is much like testcase 6b2 (where commit B moves all
#        the original paths out of z/ but opted to keep d within z/).
#
#        It was not clear in the past what should be done with this testcase;
#        in fact, I noted that I "just picked one" previously.  However,
#        following the new logic for testcase 6b2, we should take the rename
#        and move z/d to y/d.
#
#        6b1, 6b2, and this case are definitely somewhat fuzzy in terms of
#        whether they are optimal for end users, but (a) the default for
#        directory rename detection is to mark these all as conflicts
#        anyway, (b) it feels like this is less prone to higher order corner
#        case confusion, and (c) the current algorithm requires less global
#        knowledge (i.e. less coupling in the algorithm between renames done
#        on both sides) which thus means users are better able to predict
#        the behavior, and predict it without computing as many details.

test_setup_8e () {
	git init 8e &&
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
}

test_expect_success '8e: Both sides rename, one side adds to original directory' '
	test_setup_8e &&
	(
		cd 8e &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		test_grep CONFLICT.*rename/rename.*z/c.*y/c.*w/c out &&
		test_grep CONFLICT.*rename/rename.*z/b.*y/b.*w/b out &&

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

test_setup_9a () {
	git init 9a &&
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
}

test_expect_success '9a: Inner renamed directory within outer renamed directory' '
	test_setup_9a &&
	(
		cd 9a &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_9b () {
	git init 9b &&
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
}

test_expect_success '9b: Transitive rename with content merge' '
	test_setup_9b &&
	(
		cd 9b &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_9c () {
	git init 9c &&
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
}

test_expect_success '9c: Doubly transitive rename?' '
	test_setup_9c &&
	(
		cd 9c &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "WARNING: Avoiding applying x -> z rename to x/f" out &&

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

test_setup_9d () {
	git init 9d &&
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
}

test_expect_success '9d: N-way transitive rename?' '
	test_setup_9d &&
	(
		cd 9d &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
		test_grep "WARNING: Avoiding applying z -> y rename to z/t" out &&
		test_grep "WARNING: Avoiding applying y -> x rename to y/a" out &&
		test_grep "WARNING: Avoiding applying x -> w rename to x/b" out &&
		test_grep "WARNING: Avoiding applying w -> v rename to w/c" out &&

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

test_setup_9e () {
	git init 9e &&
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
}

test_expect_success '9e: N-to-1 whammo' '
	test_setup_9e &&
	(
		cd 9e &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out &&
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

test_setup_9f () {
	git init 9f &&
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
}

test_expect_success '9f: Renamed directory that only contained immediate subdirs' '
	test_setup_9f &&
	(
		cd 9f &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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
# We currently fail this test because the directory renames we detect are
#   goal/a/ -> priority/alpha/
#   goal/b/ -> priority/bravo/
# We do not detect
#   goal/   -> priority/
# because of no files found within goal/, and the fact that "a" != "alpha"
# and "b" != "bravo".  But I'm not sure it's really a failure given that
# viewpoint...

test_setup_9g () {
	git init 9g &&
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
}

test_expect_failure '9g: Renamed directory that only contained immediate subdirs, immediate subdirs renamed' '
	test_setup_9g &&
	(
		cd 9g &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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
test_setup_9h () {
	git init 9h &&
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
}

test_expect_success '9h: Avoid dir rename on merely modified path' '
	test_setup_9h &&
	(
		cd 9h &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

test_setup_10a () {
	git init 10a &&
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
}

test_expect_success '10a: Overwrite untracked with normal rename/delete' '
	test_setup_10a &&
	(
		cd 10a &&

		git checkout A^0 &&
		echo very >z/c &&
		echo important >z/d &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		test_path_is_missing .git/MERGE_HEAD &&
		test_grep "The following untracked working tree files would be overwritten by merge" err &&

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

test_setup_10b () {
	git init 10b &&
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
}

test_expect_success '10b: Overwrite untracked with dir rename + delete' '
	test_setup_10b &&
	(
		cd 10b &&

		git checkout A^0 &&
		echo very >y/c &&
		echo important >y/d &&
		echo contents >y/e &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: The following untracked working tree files would be overwritten by merge" err &&

			git ls-files -s >out &&
			test_line_count = 1 out &&
			git ls-files -u >out &&
			test_line_count = 0 out &&
			git ls-files -o >out &&
			test_line_count = 5 out
		else
			test_grep "CONFLICT (rename/delete).*Version B\^0 of y/d left in tree at y/d~B\^0" out &&
			test_grep "Error: Refusing to lose untracked file at y/e; writing to y/e~B\^0 instead" out &&

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
			test_cmp expect actual
		fi &&

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

test_setup_10c () {
	git init 10c_$1 &&
	(
		cd 10c_$1 &&

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
}

test_expect_success '10c1: Overwrite untracked with dir rename/rename(1to2)' '
	test_setup_10c 1 &&
	(
		cd 10c_1 &&

		git checkout A^0 &&
		echo important >y/c &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: The following untracked working tree files would be overwritten by merge" err &&

			git ls-files -s >out &&
			test_line_count = 4 out &&
			git ls-files -u >out &&
			test_line_count = 0 out &&
			git ls-files -o >out &&
			test_line_count = 3 out
		else
			test_grep "CONFLICT (rename/rename)" out &&
			test_grep "Refusing to lose untracked file at y/c; adding as y/c~B\^0 instead" out &&

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
			test_cmp expect actual
		fi &&

		echo important >expect &&
		test_cmp expect y/c
	)
'

test_expect_success '10c2: Overwrite untracked with dir rename/rename(1to2), other direction' '
	test_setup_10c 2 &&
	(
		cd 10c_2 &&

		git reset --hard &&
		git clean -fdqx &&

		git checkout B^0 &&
		mkdir y &&
		echo important >y/c &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive A^0 >out 2>err &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: The following untracked working tree files would be overwritten by merge" err &&

			git ls-files -s >out &&
			test_line_count = 4 out &&
			git ls-files -u >out &&
			test_line_count = 0 out &&
			git ls-files -o >out &&
			test_line_count = 3 out
		else
			test_grep "CONFLICT (rename/rename)" out &&
			test_grep "Refusing to lose untracked file at y/c; adding as y/c~HEAD instead" out &&

			git ls-files -s >out &&
			test_line_count = 6 out &&
			git ls-files -u >out &&
			test_line_count = 3 out &&
			git ls-files -o >out &&
			test_line_count = 3 out &&

			git rev-parse >actual \
				:0:y/a :0:y/b :0:x/d :1:x/c :3:w/c :2:y/c &&
			git rev-parse >expect \
				 O:z/a  O:z/b  O:x/d  O:x/c  O:x/c  O:x/c &&
			test_cmp expect actual &&

			git hash-object y/c~HEAD >actual &&
			git rev-parse O:x/c >expect &&
			test_cmp expect actual
		fi &&

		echo important >expect &&
		test_cmp expect y/c
	)
'

# Testcase 10d, Delete untracked w/ dir rename/rename(2to1)
#   Commit O: z/{a,b,c_1},        x/{d,e,f_2}
#   Commit A: y/{a,b},            x/{d,e,f_2,wham_1} + untracked y/wham
#   Commit B: z/{a,b,c_1,wham_2}, y/{d,e}
#   Expected: Failed Merge; y/{a,b,d,e} + untracked y/{wham,wham~merged}+
#             CONFLICT(rename/rename) z/c_1 vs x/f_2 -> y/wham
#             ERROR_MSG(Refusing to lose untracked file at y/wham)

test_setup_10d () {
	git init 10d &&
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
}

test_expect_success '10d: Delete untracked with dir rename/rename(2to1)' '
	test_setup_10d &&
	(
		cd 10d &&

		git checkout A^0 &&
		echo important >y/wham &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: The following untracked working tree files would be overwritten by merge" err &&

			git ls-files -s >out &&
			test_line_count = 6 out &&
			git ls-files -u >out &&
			test_line_count = 0 out &&
			git ls-files -o >out &&
			test_line_count = 3 out
		else
			test_grep "CONFLICT (rename/rename)" out &&
			test_grep "Refusing to lose untracked file at y/wham" out &&

			git ls-files -s >out &&
			test_line_count = 6 out &&
			git ls-files -u >out &&
			test_line_count = 2 out &&
			git ls-files -o >out &&
			test_line_count = 3 out &&

			git rev-parse >actual \
				:0:y/a :0:y/b :0:y/d :0:y/e :2:y/wham :3:y/wham &&
			git rev-parse >expect \
				 O:z/a  O:z/b  O:x/d  O:x/e  O:z/c     O:x/f &&
			test_cmp expect actual &&

			test_must_fail git rev-parse :1:y/wham &&

			# Test that two-way merge in y/wham~merged is as expected
			git cat-file -p :2:y/wham >expect &&
			git cat-file -p :3:y/wham >other &&
			>empty &&
			test_must_fail git merge-file \
				-L "HEAD" \
				-L "" \
				-L "B^0" \
				expect empty other &&
			test_cmp expect y/wham~merged
		fi &&

		echo important >expect &&
		test_cmp expect y/wham
	)
'

# Testcase 10e, Does git complain about untracked file that's not in the way?
#   Commit O: z/{a,b}
#   Commit A: y/{a,b} + untracked z/c
#   Commit B: z/{a,b,c}
#   Expected: y/{a,b,c} + untracked z/c

test_setup_10e () {
	git init 10e &&
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
}

test_expect_merge_algorithm failure success '10e: Does git complain about untracked file that is not really in the way?' '
	test_setup_10e &&
	(
		cd 10e &&

		git checkout A^0 &&
		mkdir z &&
		echo random >z/c &&

		git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		test_grep ! "following untracked working tree files would be overwritten by merge" err &&

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

test_setup_11a () {
	git init 11a &&
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
}

test_expect_success '11a: Avoid losing dirty contents with simple rename' '
	test_setup_11a &&
	(
		cd 11a &&

		git checkout A^0 &&
		echo stuff >>z/c &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: Your local changes to the following files would be overwritten by merge" err
		else
			test_grep "Refusing to lose dirty file at z/c" out &&

			git ls-files -s >out &&
			test_line_count = 2 out &&
			git ls-files -u >out &&
			test_line_count = 1 out &&
			git ls-files -o >out &&
			test_line_count = 3 out &&

			git rev-parse >actual \
				:0:z/a :2:z/c &&
			git rev-parse >expect \
				 O:z/a  B:z/b &&
			test_cmp expect actual &&

			git hash-object z/c~HEAD >actual &&
			git rev-parse B:z/b >expect &&
			test_cmp expect actual
		fi &&

		test_seq 1 10 >expected &&
		echo stuff >>expected &&
		test_cmp expected z/c

	)
'

# Testcase 11b, Avoid losing dirty file involved in directory rename
#   Commit O: z/a,         x/{b,c_v1}
#   Commit A: z/{a,c_v1},  x/b,       and z/c_v1 has uncommitted mods
#   Commit B: y/a,         x/{b,c_v2}
#   Expected: y/{a,c_v2}, x/b, z/c_v1 with uncommitted mods untracked,
#             ERROR_MSG(Refusing to lose dirty file at z/c)


test_setup_11b () {
	git init 11b &&
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
}

test_expect_success '11b: Avoid losing dirty file involved in directory rename' '
	test_setup_11b &&
	(
		cd 11b &&

		git checkout A^0 &&
		echo stuff >>z/c &&

		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: Your local changes to the following files would be overwritten by merge" err
		else
			git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
			test_grep "Refusing to lose dirty file at z/c" out &&

			git ls-files -s >out &&
			test_line_count = 3 out &&
			git ls-files -u >out &&
			test_line_count = 0 out &&
			git ls-files -m >out &&
			test_line_count = 0 out &&
			git ls-files -o >out &&
			test_line_count = 3 out &&

			git rev-parse >actual \
				:0:x/b :0:y/a :0:y/c &&
			git rev-parse >expect \
				 O:x/b  O:z/a  B:x/c &&
			test_cmp expect actual &&

			git hash-object y/c >actual &&
			git rev-parse B:x/c >expect &&
			test_cmp expect actual
		fi &&

		grep -q stuff z/c &&
		test_seq 1 10 >expected &&
		echo stuff >>expected &&
		test_cmp expected z/c
	)
'

# Testcase 11c, Avoid losing not-up-to-date with rename + D/F conflict
#   Commit O: y/a,         x/{b,c_v1}
#   Commit A: y/{a,c_v1},  x/b,       and y/c_v1 has uncommitted mods
#   Commit B: y/{a,c/d},   x/{b,c_v2}
#   Expected: Abort_msg("following files would be overwritten by merge") +
#             y/c left untouched (still has uncommitted mods)

test_setup_11c () {
	git init 11c &&
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
}

test_expect_success '11c: Avoid losing not-uptodate with rename + D/F conflict' '
	test_setup_11c &&
	(
		cd 11c &&

		git checkout A^0 &&
		echo stuff >>y/c &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: Your local changes to the following files would be overwritten by merge" err
		else
			test_grep "following files would be overwritten by merge" err
		fi &&

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

test_setup_11d () {
	git init 11d &&
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
}

test_expect_success '11d: Avoid losing not-uptodate with rename + D/F conflict' '
	test_setup_11d &&
	(
		cd 11d &&

		git checkout A^0 &&
		echo stuff >>z/c &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: Your local changes to the following files would be overwritten by merge" err
		else
			test_grep "Refusing to lose dirty file at z/c" out &&

			git ls-files -s >out &&
			test_line_count = 4 out &&
			git ls-files -u >out &&
			test_line_count = 1 out &&
			git ls-files -o >out &&
			test_line_count = 4 out &&

			git rev-parse >actual \
				:0:x/b :0:y/a :0:y/c/d :3:y/c &&
			git rev-parse >expect \
				 O:x/b  O:z/a  B:y/c/d  B:x/c &&
			test_cmp expect actual &&

			git hash-object y/c~HEAD >actual &&
			git rev-parse B:x/c >expect &&
			test_cmp expect actual
		fi &&

		grep -q stuff z/c &&
		test_seq 1 10 >expected &&
		echo stuff >>expected &&
		test_cmp expected z/c
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

test_setup_11e () {
	git init 11e &&
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
}

test_expect_success '11e: Avoid deleting not-uptodate with dir rename/rename(1to2)/add' '
	test_setup_11e &&
	(
		cd 11e &&

		git checkout A^0 &&
		echo mods >>y/c &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: Your local changes to the following files would be overwritten by merge" err
		else
			test_grep "CONFLICT (rename/rename)" out &&
			test_grep "Refusing to lose dirty file at y/c" out &&

			git ls-files -s >out &&
			test_line_count = 7 out &&
			git ls-files -u >out &&
			test_line_count = 4 out &&
			git ls-files -o >out &&
			test_line_count = 3 out &&

			git rev-parse >actual \
				:0:y/a :0:y/b :0:x/d :1:x/c :2:w/c :2:y/c :3:y/c &&
			git rev-parse >expect \
				 O:z/a  O:z/b  O:x/d  O:x/c  O:x/c  A:y/c  O:x/c &&
			test_cmp expect actual &&

			# See if y/c~merged has expected contents; requires manually
			# doing the expected file merge
			git cat-file -p A:y/c >c1 &&
			git cat-file -p B:z/c >c2 &&
			>empty &&
			test_must_fail git merge-file \
				-L "HEAD" \
				-L "" \
				-L "B^0" \
				c1 empty c2 &&
			test_cmp c1 y/c~merged
		fi &&

		echo different >expected &&
		echo mods >>expected &&
		test_cmp expected y/c
	)
'

# Testcase 11f, Avoid deleting not-up-to-date w/ dir rename/rename(2to1)
#   Commit O: z/{a,b},        x/{c_1,d_2}
#   Commit A: y/{a,b,wham_1}, x/d_2, except y/wham has uncommitted mods
#   Commit B: z/{a,b,wham_2}, x/c_1
#   Expected: Failed Merge; y/{a,b} + untracked y/{wham~merged} +
#             y/wham with dirty changes from before merge +
#             CONFLICT(rename/rename) x/c vs x/d -> y/wham
#             ERROR_MSG(Refusing to lose dirty file at y/wham)

test_setup_11f () {
	git init 11f &&
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
}

test_expect_success '11f: Avoid deleting not-uptodate with dir rename/rename(2to1)' '
	test_setup_11f &&
	(
		cd 11f &&

		git checkout A^0 &&
		echo important >>y/wham &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&
		if test "$GIT_TEST_MERGE_ALGORITHM" = ort
		then
			test_path_is_missing .git/MERGE_HEAD &&
			test_grep "error: Your local changes to the following files would be overwritten by merge" err
		else
			test_grep "CONFLICT (rename/rename)" out &&
			test_grep "Refusing to lose dirty file at y/wham" out &&

			git ls-files -s >out &&
			test_line_count = 4 out &&
			git ls-files -u >out &&
			test_line_count = 2 out &&
			git ls-files -o >out &&
			test_line_count = 3 out &&

			test_must_fail git rev-parse :1:y/wham &&

			git rev-parse >actual \
				:0:y/a :0:y/b :2:y/wham :3:y/wham &&
			git rev-parse >expect \
				 O:z/a  O:z/b  O:x/c     O:x/d &&
			test_cmp expect actual &&

			# Test that two-way merge in y/wham~merged is as expected
			git cat-file -p :2:y/wham >expect &&
			git cat-file -p :3:y/wham >other &&
			>empty &&
			test_must_fail git merge-file \
				-L "HEAD" \
				-L "" \
				-L "B^0" \
				expect empty other &&
			test_cmp expect y/wham~merged
		fi &&

		test_seq 1 10 >expected &&
		echo important >>expected &&
		test_cmp expected y/wham
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

test_setup_12a () {
	git init 12a &&
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
}

test_expect_success '12a: Moving one directory hierarchy into another' '
	test_setup_12a &&
	(
		cd 12a &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

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

# Testcase 12b1, Moving two directory hierarchies into each other
#   (Related to testcases 1c and 12c)
#   Commit O: node1/{leaf1, leaf2}, node2/{leaf3, leaf4}
#   Commit A: node1/{leaf1, leaf2, node2/{leaf3, leaf4}}
#   Commit B: node2/{leaf3, leaf4, node1/{leaf1, leaf2}}
#   Expected: node1/node2/{leaf3, leaf4}
#             node2/node1/{leaf1, leaf2}
#   NOTE: If there were new files added to the old node1/ or node2/ directories,
#         then we would need to detect renames for those directories and would
#         find that:
#             commit A renames node2/ -> node1/node2/
#             commit B renames node1/ -> node2/node1/
#         Applying those directory renames to the initial result (making all
#         four paths experience a transitive renaming), yields
#             node1/node2/node1/{leaf1, leaf2}
#             node2/node1/node2/{leaf3, leaf4}
#         as the result.  It may be really weird to have two directories
#         rename each other, but simple rules give weird results when given
#         weird inputs.  HOWEVER, the "If" at the beginning of those NOTE was
#         false; there were no new files added and thus there is no directory
#         rename detection to perform.  As such, we just have simple renames
#         and the expected answer is:
#             node1/node2/{leaf3, leaf4}
#             node2/node1/{leaf1, leaf2}

test_setup_12b1 () {
	git init 12b1 &&
	(
		cd 12b1 &&

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
}

test_expect_merge_algorithm failure success '12b1: Moving two directory hierarchies into each other' '
	test_setup_12b1 &&
	(
		cd 12b1 &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			HEAD:node2/node1/leaf1 \
			HEAD:node2/node1/leaf2 \
			HEAD:node1/node2/leaf3 \
			HEAD:node1/node2/leaf4 &&
		git rev-parse >expect \
			O:node1/leaf1 \
			O:node1/leaf2 \
			O:node2/leaf3 \
			O:node2/leaf4 &&
		test_cmp expect actual
	)
'

# Testcase 12b2, Moving two directory hierarchies into each other
#   (Related to testcases 1c and 12c)
#   Commit O: node1/{leaf1, leaf2}, node2/{leaf3, leaf4}
#   Commit A: node1/{leaf1, leaf2, leaf5, node2/{leaf3, leaf4}}
#   Commit B: node2/{leaf3, leaf4, leaf6, node1/{leaf1, leaf2}}
#   Expected: node1/node2/{node1/{leaf1, leaf2}, leaf6}
#             node2/node1/{node2/{leaf3, leaf4}, leaf5}
#   NOTE: Without directory renames, we would expect
#             A: node2/leaf3 -> node1/node2/leaf3
#             A: node2/leaf1 -> node1/node2/leaf4
#             A: Adds           node1/leaf5
#             B: node1/leaf1 -> node2/node1/leaf1
#             B: node1/leaf2 -> node2/node1/leaf2
#             B: Adds           node2/leaf6
#         with directory rename detection, we note that
#             commit A renames node2/ -> node1/node2/
#             commit B renames node1/ -> node2/node1/
#         therefore, applying A's directory rename to the paths added in B gives:
#             B: node1/leaf1 -> node1/node2/node1/leaf1
#             B: node1/leaf2 -> node1/node2/node1/leaf2
#             B: Adds           node1/node2/leaf6
#         and applying B's directory rename to the paths added in A gives:
#             A: node2/leaf3 -> node2/node1/node2/leaf3
#             A: node2/leaf1 -> node2/node1/node2/leaf4
#             A: Adds           node2/node1/leaf5
#         resulting in the expected
#             node1/node2/{node1/{leaf1, leaf2}, leaf6}
#             node2/node1/{node2/{leaf3, leaf4}, leaf5}
#
#         You may ask, is it weird to have two directories rename each other?
#         To which, I can do no more than shrug my shoulders and say that
#         even simple rules give weird results when given weird inputs.

test_setup_12b2 () {
	git init 12b2 &&
	(
		cd 12b2 &&

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
		echo leaf5 >node1/leaf5 &&
		git add node1/leaf5 &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv node1/ node2/ &&
		echo leaf6 >node2/leaf6 &&
		git add node2/leaf6 &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_success '12b2: Moving two directory hierarchies into each other' '
	test_setup_12b2 &&
	(
		cd 12b2 &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 6 out &&

		git rev-parse >actual \
			HEAD:node1/node2/node1/leaf1 \
			HEAD:node1/node2/node1/leaf2 \
			HEAD:node2/node1/node2/leaf3 \
			HEAD:node2/node1/node2/leaf4 \
			HEAD:node2/node1/leaf5       \
			HEAD:node1/node2/leaf6       &&
		git rev-parse >expect \
			O:node1/leaf1 \
			O:node1/leaf2 \
			O:node2/leaf3 \
			O:node2/leaf4 \
			A:node1/leaf5 \
			B:node2/leaf6 &&
		test_cmp expect actual
	)
'

# Testcase 12c1, Moving two directory hierarchies into each other w/ content merge
#   (Related to testcase 12b)
#   Commit O: node1/{       leaf1_1, leaf2_1}, node2/{leaf3_1, leaf4_1}
#   Commit A: node1/{       leaf1_2, leaf2_2,  node2/{leaf3_2, leaf4_2}}
#   Commit B: node2/{node1/{leaf1_3, leaf2_3},        leaf3_3, leaf4_3}
#   Expected: Content merge conflicts for each of:
#               node1/node2/node1/{leaf1, leaf2},
#               node2/node1/node2/{leaf3, leaf4}
#   NOTE: This is *exactly* like 12b1, except that every path is modified on
#         each side of the merge.

test_setup_12c1 () {
	git init 12c1 &&
	(
		cd 12c1 &&

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
		for i in $(git ls-files); do echo side A >>$i; done &&
		git add -u &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv node1/ node2/ &&
		for i in $(git ls-files); do echo side B >>$i; done &&
		git add -u &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_merge_algorithm failure success '12c1: Moving one directory hierarchy into another w/ content merge' '
	test_setup_12c1 &&
	(
		cd 12c1 &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 &&

		git ls-files -u >out &&
		test_line_count = 12 out &&

		git rev-parse >actual \
			:1:node2/node1/leaf1 \
			:1:node2/node1/leaf2 \
			:1:node1/node2/leaf3 \
			:1:node1/node2/leaf4 \
			:2:node2/node1/leaf1 \
			:2:node2/node1/leaf2 \
			:2:node1/node2/leaf3 \
			:2:node1/node2/leaf4 \
			:3:node2/node1/leaf1 \
			:3:node2/node1/leaf2 \
			:3:node1/node2/leaf3 \
			:3:node1/node2/leaf4 &&
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

# Testcase 12c2, Moving two directory hierarchies into each other w/ content merge
#   (Related to testcase 12b)
#   Commit O: node1/{       leaf1_1, leaf2_1}, node2/{leaf3_1, leaf4_1}
#   Commit A: node1/{       leaf1_2, leaf2_2,  node2/{leaf3_2, leaf4_2}, leaf5}
#   Commit B: node2/{node1/{leaf1_3, leaf2_3},        leaf3_3, leaf4_3,  leaf6}
#   Expected: Content merge conflicts for each of:
#               node1/node2/node1/{leaf1, leaf2}
#               node2/node1/node2/{leaf3, leaf4}
#             plus
#               node2/node1/leaf5
#               node1/node2/leaf6
#   NOTE: This is *exactly* like 12b2, except that every path from O is modified
#         on each side of the merge.

test_setup_12c2 () {
	git init 12c2 &&
	(
		cd 12c2 &&

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
		for i in $(git ls-files); do echo side A >>$i; done &&
		git add -u &&
		echo leaf5 >node1/leaf5 &&
		git add node1/leaf5 &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv node1/ node2/ &&
		for i in $(git ls-files); do echo side B >>$i; done &&
		git add -u &&
		echo leaf6 >node2/leaf6 &&
		git add node2/leaf6 &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_success '12c2: Moving one directory hierarchy into another w/ content merge' '
	test_setup_12c2 &&
	(
		cd 12c2 &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 14 out &&
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
			:3:node2/node1/node2/leaf4 \
			:0:node2/node1/leaf5       \
			:0:node1/node2/leaf6       &&
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
			B:node2/leaf4 \
			A:node1/leaf5 \
			B:node2/leaf6 &&
		test_cmp expect actual
	)
'

# Testcase 12d, Rename/merge of subdirectory into the root
#   Commit O: a/b/subdir/foo
#   Commit A: subdir/foo
#   Commit B: a/b/subdir/foo, a/b/bar
#   Expected: subdir/foo, bar

test_setup_12d () {
	git init 12d &&
	(
		cd 12d &&

		mkdir -p a/b/subdir &&
		test_commit a/b/subdir/foo &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir subdir &&
		git mv a/b/subdir/foo.t subdir/foo.t &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		test_commit a/b/bar
	)
}

test_expect_success '12d: Rename/merge subdir into the root, variant 1' '
	test_setup_12d &&
	(
		cd 12d &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&

		git rev-parse >actual \
			HEAD:subdir/foo.t   HEAD:bar.t &&
		git rev-parse >expect \
			O:a/b/subdir/foo.t  B:a/b/bar.t &&
		test_cmp expect actual &&

		git hash-object bar.t >actual &&
		git rev-parse B:a/b/bar.t >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:a/b/subdir/foo.t &&
		test_must_fail git rev-parse HEAD:a/b/bar.t &&
		test_path_is_missing a/ &&
		test_path_is_file bar.t
	)
'

# Testcase 12e, Rename/merge of subdirectory into the root
#   Commit O: a/b/foo
#   Commit A: foo
#   Commit B: a/b/foo, a/b/bar
#   Expected: foo, bar

test_setup_12e () {
	git init 12e &&
	(
		cd 12e &&

		mkdir -p a/b &&
		test_commit a/b/foo &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		mkdir subdir &&
		git mv a/b/foo.t foo.t &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		test_commit a/b/bar
	)
}

test_expect_success '12e: Rename/merge subdir into the root, variant 2' '
	test_setup_12e &&
	(
		cd 12e &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

		git ls-files -s >out &&
		test_line_count = 2 out &&

		git rev-parse >actual \
			HEAD:foo.t   HEAD:bar.t &&
		git rev-parse >expect \
			O:a/b/foo.t  B:a/b/bar.t &&
		test_cmp expect actual &&

		git hash-object bar.t >actual &&
		git rev-parse B:a/b/bar.t >expect &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:a/b/foo.t &&
		test_must_fail git rev-parse HEAD:a/b/bar.t &&
		test_path_is_missing a/ &&
		test_path_is_file bar.t
	)
'

# Testcase 12f, Rebase of patches with big directory rename
#   Commit O:
#              dir/subdir/{a,b,c,d,e_O,Makefile_TOP_O}
#              dir/subdir/tweaked/{f,g,h,Makefile_SUB_O}
#              dir/unchanged/<LOTS OF FILES>
#   Commit A:
#     (Remove f & g, move e into newsubdir, rename dir/->folder/, modify files)
#              folder/subdir/{a,b,c,d,Makefile_TOP_A}
#              folder/subdir/newsubdir/e_A
#              folder/subdir/tweaked/{h,Makefile_SUB_A}
#              folder/unchanged/<LOTS OF FILES>
#   Commit B1:
#     (add newfile.{c,py}, modify underscored files)
#              dir/{a,b,c,d,e_B1,Makefile_TOP_B1,newfile.c}
#              dir/tweaked/{f,g,h,Makefile_SUB_B1,newfile.py}
#              dir/unchanged/<LOTS OF FILES>
#   Commit B2:
#     (Modify e further, add newfile.rs)
#              dir/{a,b,c,d,e_B2,Makefile_TOP_B1,newfile.c,newfile.rs}
#              dir/tweaked/{f,g,h,Makefile_SUB_B1,newfile.py}
#              dir/unchanged/<LOTS OF FILES>
#   Expected:
#          B1-picked:
#              folder/subdir/{a,b,c,d,Makefile_TOP_Merge1,newfile.c}
#              folder/subdir/newsubdir/e_Merge1
#              folder/subdir/tweaked/{h,Makefile_SUB_Merge1,newfile.py}
#              folder/unchanged/<LOTS OF FILES>
#          B2-picked:
#              folder/subdir/{a,b,c,d,Makefile_TOP_Merge1,newfile.c,newfile.rs}
#              folder/subdir/newsubdir/e_Merge2
#              folder/subdir/tweaked/{h,Makefile_SUB_Merge1,newfile.py}
#              folder/unchanged/<LOTS OF FILES>
# Things being checked here:
#   1. dir/subdir/newfile.c does not get pushed into folder/subdir/newsubdir/.
#      dir/subdir/{a,b,c,d} -> folder/subdir/{a,b,c,d} looks like
#          dir/ -> folder/,
#      whereas dir/subdir/e -> folder/subdir/newsubdir/e looks like
#          dir/subdir/ -> folder/subdir/newsubdir/
#      and if we note that newfile.c is found in dir/subdir/, we might overlook
#      the dir/ -> folder/ rule that has more weight.  Older git versions did
#      this.
#   2. The code to do trivial directory resolves.  Note that
#      dir/subdir/unchanged/ is unchanged and can be deleted, and files in the
#      new folder/subdir/unchanged/ are not needed as a target to any renames.
#      Thus, in the second collect_merge_info_callback() we can just resolve
#      these two directories trivially without recursing.)
#   3. Exercising the codepaths for caching renames and deletes from one cherry
#      pick and re-applying them in the subsequent one.

test_setup_12f () {
	git init 12f &&
	(
		cd 12f &&

		mkdir -p dir/unchanged &&
		mkdir -p dir/subdir/tweaked &&
		echo a >dir/subdir/a &&
		echo b >dir/subdir/b &&
		echo c >dir/subdir/c &&
		echo d >dir/subdir/d &&
		test_seq 1 10 >dir/subdir/e &&
		test_seq 10 20 >dir/subdir/Makefile &&
		echo f >dir/subdir/tweaked/f &&
		echo g >dir/subdir/tweaked/g &&
		echo h >dir/subdir/tweaked/h &&
		test_seq 20 30 >dir/subdir/tweaked/Makefile &&
		for i in $(test_seq 1 88); do
			echo content $i >dir/unchanged/file_$i
		done &&
		git add . &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git switch A &&
		git rm dir/subdir/tweaked/f dir/subdir/tweaked/g &&
		test_seq 2 10 >dir/subdir/e &&
		test_seq 11 20 >dir/subdir/Makefile &&
		test_seq 21 30 >dir/subdir/tweaked/Makefile &&
		mkdir dir/subdir/newsubdir &&
		git mv dir/subdir/e dir/subdir/newsubdir/ &&
		git mv dir folder &&
		git add . &&
		git commit -m "A" &&

		git switch B &&
		mkdir dir/subdir/newsubdir/ &&
		echo c code >dir/subdir/newfile.c &&
		echo python code >dir/subdir/newsubdir/newfile.py &&
		test_seq 1 11 >dir/subdir/e &&
		test_seq 10 21 >dir/subdir/Makefile &&
		test_seq 20 31 >dir/subdir/tweaked/Makefile &&
		git add . &&
		git commit -m "B1" &&

		echo rust code >dir/subdir/newfile.rs &&
		test_seq 1 12 >dir/subdir/e &&
		git add . &&
		git commit -m "B2"
	)
}

test_expect_merge_algorithm failure success '12f: Trivial directory resolve, caching, all kinds of fun' '
	test_setup_12f &&
	(
		cd 12f &&

		git checkout A^0 &&
		git branch Bmod B &&

		GIT_TRACE2_PERF="$(pwd)/trace.output" git -c merge.directoryRenames=true rebase A Bmod &&

		echo Checking the pick of B1... &&

		test_must_fail git rev-parse Bmod~1:dir &&

		git ls-tree -r Bmod~1 >out &&
		test_line_count = 98 out &&

		git diff --name-status A Bmod~1 >actual &&
		q_to_tab >expect <<-\EOF &&
		MQfolder/subdir/Makefile
		AQfolder/subdir/newfile.c
		MQfolder/subdir/newsubdir/e
		AQfolder/subdir/newsubdir/newfile.py
		MQfolder/subdir/tweaked/Makefile
		EOF
		test_cmp expect actual &&

		# Three-way merged files
		test_seq  2 11 >e_Merge1 &&
		test_seq 11 21 >Makefile_TOP &&
		test_seq 21 31 >Makefile_SUB &&
		git hash-object >expect      \
			e_Merge1             \
			Makefile_TOP         \
			Makefile_SUB         &&
		git rev-parse >actual              \
			Bmod~1:folder/subdir/newsubdir/e     \
			Bmod~1:folder/subdir/Makefile        \
			Bmod~1:folder/subdir/tweaked/Makefile &&
		test_cmp expect actual &&

		# New files showed up at the right location with right contents
		git rev-parse >expect                \
			B~1:dir/subdir/newfile.c            \
			B~1:dir/subdir/newsubdir/newfile.py &&
		git rev-parse >actual                      \
			Bmod~1:folder/subdir/newfile.c            \
			Bmod~1:folder/subdir/newsubdir/newfile.py &&
		test_cmp expect actual &&

		# Removed files
		test_path_is_missing folder/subdir/tweaked/f &&
		test_path_is_missing folder/subdir/tweaked/g &&

		# Unchanged files or directories
		git rev-parse >actual        \
			Bmod~1:folder/subdir/a          \
			Bmod~1:folder/subdir/b          \
			Bmod~1:folder/subdir/c          \
			Bmod~1:folder/subdir/d          \
			Bmod~1:folder/unchanged         \
			Bmod~1:folder/subdir/tweaked/h &&
		git rev-parse >expect          \
			O:dir/subdir/a         \
			O:dir/subdir/b         \
			O:dir/subdir/c         \
			O:dir/subdir/d         \
			O:dir/unchanged        \
			O:dir/subdir/tweaked/h &&
		test_cmp expect actual &&

		echo Checking the pick of B2... &&

		test_must_fail git rev-parse Bmod:dir &&

		git ls-tree -r Bmod >out &&
		test_line_count = 99 out &&

		git diff --name-status Bmod~1 Bmod >actual &&
		q_to_tab >expect <<-\EOF &&
		AQfolder/subdir/newfile.rs
		MQfolder/subdir/newsubdir/e
		EOF
		test_cmp expect actual &&

		# Three-way merged file
		test_seq  2 12 >e_Merge2 &&
		git hash-object e_Merge2 >expect &&
		git rev-parse Bmod:folder/subdir/newsubdir/e >actual &&
		test_cmp expect actual &&

		grep region_enter.*collect_merge_info trace.output >collect &&
		test_line_count = 4 collect &&
		grep region_enter.*process_entries$ trace.output >process &&
		test_line_count = 2 process
	)
'

# Testcase 12g, Testcase with two kinds of "relevant" renames
#   Commit O: somefile_O, subdir/{a_O,b_O}
#   Commit A: somefile_A, subdir/{a_O,b_O,c_A}
#   Commit B: newfile_B,  newdir/{a_B,b_B}
#   Expected: newfile_{merged}, newdir/{a_B,b_B,c_A}

test_setup_12g () {
	git init 12g &&
	(
		cd 12g &&

		mkdir -p subdir &&
		test_write_lines upon a time there was a >somefile &&
		test_write_lines 1 2 3 4 5 6 7 8 9 10 >subdir/a &&
		test_write_lines one two three four five six >subdir/b &&
		git add . &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git switch A &&
		test_write_lines once upon a time there was a >somefile &&
		> subdir/c &&
		git add somefile subdir/c &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv somefile newfile &&
		git mv subdir newdir &&
		echo repo >>newfile &&
		test_write_lines 1 2 3 4 5 6 7 8 9 10 11 >newdir/a &&
		test_write_lines one two three four five six seven >newdir/b &&
		git add newfile newdir &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_success '12g: Testcase with two kinds of "relevant" renames' '
	test_setup_12g &&
	(
		cd 12g &&

		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 &&

		test_write_lines once upon a time there was a repo >expect &&
		test_cmp expect newfile &&

		git ls-files -s >out &&
		test_line_count = 4 out &&

		git rev-parse >actual \
			HEAD:newdir/a  HEAD:newdir/b   HEAD:newdir/c &&
		git rev-parse >expect \
			B:newdir/a     B:newdir/b      A:subdir/c &&
		test_cmp expect actual &&

		test_must_fail git rev-parse HEAD:subdir/a &&
		test_must_fail git rev-parse HEAD:subdir/b &&
		test_must_fail git rev-parse HEAD:subdir/c &&
		test_path_is_missing subdir/ &&
		test_path_is_file newdir/c
	)
'

# Testcase 12h, Testcase with two kinds of "relevant" renames
#   Commit O: olddir/{a_1, b}
#   Commit A: newdir/{a_2, b}
#   Commit B: olddir/{alpha_1, b}
#   Expected: newdir/{alpha_2, b}

test_setup_12h () {
	git init 12h &&
	(
		cd 12h &&

		mkdir olddir &&
		test_seq 3 8 >olddir/a &&
		>olddir/b &&
		git add olddir &&
		git commit -m orig &&

		git branch O &&
		git branch A &&
		git branch B &&

		git switch A &&
		test_seq 3 10 >olddir/a &&
		git add olddir/a &&
		git mv olddir newdir &&
		git commit -m A &&

		git switch B &&

		git mv olddir/a olddir/alpha &&
		git commit -m B
	)
}

test_expect_failure '12h: renaming a file within a renamed directory' '
	test_setup_12h &&
	(
		cd 12h &&

		git checkout A^0 &&

		test_might_fail git -c merge.directoryRenames=true merge -s recursive B^0 &&

		git ls-files >tracked &&
		test_line_count = 2 tracked &&

		test_path_is_missing olddir/a &&
		test_path_is_file newdir/alpha &&
		test_path_is_file newdir/b &&

		git rev-parse >actual \
			HEAD:newdir/alpha  HEAD:newdir/b &&
		git rev-parse >expect \
			A:newdir/a         O:oldir/b &&
		test_cmp expect actual
	)
'

# Testcase 12i, Directory rename causes rename-to-self
#   Commit O: source/{subdir/foo, bar, baz_1}
#   Commit A: source/{foo, bar, baz_1}
#   Commit B: source/{subdir/{foo, bar}, baz_2}
#   Expected: source/{foo, bar, baz_2}, with conflicts on
#                source/bar vs. source/subdir/bar

test_setup_12i () {
	git init 12i &&
	(
		cd 12i &&

		mkdir -p source/subdir &&
		echo foo >source/subdir/foo &&
		echo bar >source/bar &&
		echo baz >source/baz &&
		git add source &&
		git commit -m orig &&

		git branch O &&
		git branch A &&
		git branch B &&

		git switch A &&
		git mv source/subdir/foo source/foo &&
		git commit -m A &&

		git switch B &&
		git mv source/bar source/subdir/bar &&
		echo more baz >>source/baz &&
		git commit -m B
	)
}

test_expect_success '12i: Directory rename causes rename-to-self' '
	test_setup_12i &&
	(
		cd 12i &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=conflict merge -s recursive B^0 &&

		test_path_is_missing source/subdir &&
		test_path_is_file source/bar &&
		test_path_is_file source/baz &&

		git ls-files | uniq >tracked &&
		test_line_count = 3 tracked &&

		git status --porcelain -uno >actual &&
		cat >expect <<-\EOF &&
		UU source/bar
		 M source/baz
		EOF
		test_cmp expect actual
	)
'

# Testcase 12j, Directory rename to root causes rename-to-self
#   Commit O: {subdir/foo, bar, baz_1}
#   Commit A: {foo, bar, baz_1}
#   Commit B: {subdir/{foo, bar}, baz_2}
#   Expected: {foo, bar, baz_2}, with conflicts on bar vs. subdir/bar

test_setup_12j () {
	git init 12j &&
	(
		cd 12j &&

		mkdir -p subdir &&
		echo foo >subdir/foo &&
		echo bar >bar &&
		echo baz >baz &&
		git add . &&
		git commit -m orig &&

		git branch O &&
		git branch A &&
		git branch B &&

		git switch A &&
		git mv subdir/foo foo &&
		git commit -m A &&

		git switch B &&
		git mv bar subdir/bar &&
		echo more baz >>baz &&
		git commit -m B
	)
}

test_expect_success '12j: Directory rename to root causes rename-to-self' '
	test_setup_12j &&
	(
		cd 12j &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=conflict merge -s recursive B^0 &&

		test_path_is_missing subdir &&
		test_path_is_file bar &&
		test_path_is_file baz &&

		git ls-files | uniq >tracked &&
		test_line_count = 3 tracked &&

		git status --porcelain -uno >actual &&
		cat >expect <<-\EOF &&
		UU bar
		 M baz
		EOF
		test_cmp expect actual
	)
'

# Testcase 12k, Directory rename with sibling causes rename-to-self
#   Commit O: dirB/foo, dirA/{bar, baz_1}
#   Commit A: dirA/{foo, bar, baz_1}
#   Commit B: dirB/{foo, bar}, dirA/baz_2
#   Expected: dirA/{foo, bar, baz_2}, with conflicts on dirA/bar vs. dirB/bar

test_setup_12k () {
	git init 12k &&
	(
		cd 12k &&

		mkdir dirA dirB &&
		echo foo >dirB/foo &&
		echo bar >dirA/bar &&
		echo baz >dirA/baz &&
		git add . &&
		git commit -m orig &&

		git branch O &&
		git branch A &&
		git branch B &&

		git switch A &&
		git mv dirB/* dirA/ &&
		git commit -m A &&

		git switch B &&
		git mv dirA/bar dirB/bar &&
		echo more baz >>dirA/baz &&
		git commit -m B
	)
}

test_expect_success '12k: Directory rename with sibling causes rename-to-self' '
	test_setup_12k &&
	(
		cd 12k &&

		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=conflict merge -s recursive B^0 &&

		test_path_is_missing dirB &&
		test_path_is_file dirA/bar &&
		test_path_is_file dirA/baz &&

		git ls-files | uniq >tracked &&
		test_line_count = 3 tracked &&

		git status --porcelain -uno >actual &&
		cat >expect <<-\EOF &&
		UU dirA/bar
		 M dirA/baz
		EOF
		test_cmp expect actual
	)
'

# Testcase 12l, Both sides rename a directory into the other side, both add
#   a file which after directory renames are the same filename
#   Commit O: sub1/file,                 sub2/other
#   Commit A: sub3/file,                 sub2/{other, new_add_add_file_1}
#   Commit B: sub1/{file, newfile}, sub1/sub2/{other, new_add_add_file_2}
#
#   In words:
#     A: sub1/ -> sub3/, add sub2/new_add_add_file_1
#     B: sub2/ -> sub1/sub2, add sub1/newfile, add sub1/sub2/new_add_add_file_2
#
#   Expected: sub3/{file, newfile, sub2/other}
#             CONFLICT (add/add): sub1/sub2/new_add_add_file
#
#   Note that sub1/newfile is not extraneous.  Directory renames are only
#   detected if they are needed, and they are only needed if the old directory
#   had a new file added on the opposite side of history.  So sub1/newfile
#   is needed for there to be a sub1/ -> sub3/ rename.

test_setup_12l () {
	git init 12l_$1 &&
	(
		cd 12l_$1 &&

		mkdir sub1 sub2
		echo file >sub1/file &&
		echo other >sub2/other &&
		git add sub1 sub2 &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv sub1 sub3 &&
		echo conflicting >sub2/new_add_add_file &&
		git add sub2 &&
		test_tick &&
		git add -u &&
		git commit -m "A" &&

		git checkout B &&
		echo dissimilar >sub2/new_add_add_file &&
		echo brand >sub1/newfile &&
		git add sub1 sub2 &&
		git mv sub2 sub1 &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_merge_algorithm failure success '12l (B into A): Rename into each other + add/add conflict' '
	test_setup_12l BintoA &&
	(
		cd 12l_BintoA &&

		git checkout -q A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 &&

		test_stdout_line_count = 5 git ls-files -s &&

		git rev-parse >actual \
			:0:sub3/file :0:sub3/newfile :0:sub3/sub2/other \
			:2:sub1/sub2/new_add_add_file \
			:3:sub1/sub2/new_add_add_file &&
		git rev-parse >expect \
			O:sub1/file  B:sub1/newfile O:sub2/other \
			A:sub2/new_add_add_file \
			B:sub1/sub2/new_add_add_file &&
		test_cmp expect actual &&

		git ls-files -o >actual &&
		test_write_lines actual expect >expect &&
		test_cmp expect actual
	)
'

test_expect_merge_algorithm failure success '12l (A into B): Rename into each other + add/add conflict' '
	test_setup_12l AintoB &&
	(
		cd 12l_AintoB &&

		git checkout -q B^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive A^0 &&

		test_stdout_line_count = 5 git ls-files -s &&

		git rev-parse >actual \
			:0:sub3/file :0:sub3/newfile :0:sub3/sub2/other \
			:2:sub1/sub2/new_add_add_file \
			:3:sub1/sub2/new_add_add_file &&
		git rev-parse >expect \
			O:sub1/file  B:sub1/newfile O:sub2/other \
			B:sub1/sub2/new_add_add_file \
			A:sub2/new_add_add_file &&
		test_cmp expect actual &&

		git ls-files -o >actual &&
		test_write_lines actual expect >expect &&
		test_cmp expect actual
	)
'

# Testcase 12m, Directory rename, plus change of parent dir to symlink
#   Commit O:  dir/subdir/file
#   Commit A:  renamed-dir/subdir/file
#   Commit B:  dir/subdir
#   In words:
#     A: dir/subdir/ -> renamed-dir/subdir
#     B: delete dir/subdir/file, add dir/subdir as symlink
#
#   Expected: CONFLICT (rename/delete): renamed-dir/subdir/file,
#             CONFLICT (file location): renamed-dir/subdir vs. dir/subdir
#             CONFLICT (directory/file): renamed-dir/subdir symlink has
#                                        renamed-dir/subdir in the way

test_setup_12m () {
	git init 12m &&
	(
		cd 12m &&

		mkdir -p dir/subdir &&
		echo 1 >dir/subdir/file &&
		git add . &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git switch A &&
		git mv dir/ renamed-dir/ &&
		git add . &&
		git commit -m "A" &&

		git switch B &&
		git rm dir/subdir/file &&
		mkdir dir &&
		ln -s /dev/null dir/subdir &&
		git add . &&
		git commit -m "B"
	)
}

test_expect_merge_algorithm failure success '12m: Change parent of renamed-dir to symlink on other side' '
	test_setup_12m &&
	(
		cd 12m &&

		git checkout -q A^0 &&

		test_must_fail git -c merge.directoryRenames=conflict merge -s recursive B^0 &&

		test_stdout_line_count = 3 git ls-files -s &&
		test_stdout_line_count = 2 ls -1 renamed-dir &&
		test_path_is_missing dir
	)
'

###########################################################################
# SECTION 13: Checking informational and conflict messages
#
# A year after directory rename detection became the default, it was
# instead decided to report conflicts on the pathname on the basis that
# some users may expect the new files added or moved into a directory to
# be unrelated to all the other files in that directory, and thus that
# directory rename detection is unexpected.  Test that the messages printed
# match our expectation.
###########################################################################

# Testcase 13a, Basic directory rename with newly added files
#   Commit O: z/{b,c}
#   Commit A: y/{b,c}
#   Commit B: z/{b,c,d,e/f}
#   Expected: y/{b,c,d,e/f}, with notices/conflicts for both y/d and y/e/f

test_setup_13a () {
	git init 13a_$1 &&
	(
		cd 13a_$1 &&

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
}

test_expect_success '13a(conflict): messages for newly added files' '
	test_setup_13a conflict &&
	(
		cd 13a_conflict &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&

		test_grep CONFLICT..file.location.*z/e/f.added.in.B^0.*y/e/f out &&
		test_grep CONFLICT..file.location.*z/d.added.in.B^0.*y/d out &&

		git ls-files >paths &&
		! grep z/ paths &&
		grep "y/[de]" paths &&

		test_path_is_missing z/d &&
		test_path_is_file    y/d &&
		test_path_is_missing z/e/f &&
		test_path_is_file    y/e/f
	)
'

test_expect_success '13a(info): messages for newly added files' '
	test_setup_13a info &&
	(
		cd 13a_info &&

		git reset --hard &&
		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&

		test_grep Path.updated:.*z/e/f.added.in.B^0.*y/e/f out &&
		test_grep Path.updated:.*z/d.added.in.B^0.*y/d out &&

		git ls-files >paths &&
		! grep z/ paths &&
		grep "y/[de]" paths &&

		test_path_is_missing z/d &&
		test_path_is_file    y/d &&
		test_path_is_missing z/e/f &&
		test_path_is_file    y/e/f
	)
'

# Testcase 13b, Transitive rename with conflicted content merge and default
#               "conflict" setting
#   (Related to testcase 1c, 9b)
#   Commit O: z/{b,c},   x/d_1
#   Commit A: y/{b,c},   x/d_2
#   Commit B: z/{b,c,d_3}
#   Expected: y/{b,c,d_merged}, with two conflict messages for y/d,
#             one about content, and one about file location

test_setup_13b () {
	git init 13b_$1 &&
	(
		cd 13b_$1 &&

		mkdir x &&
		mkdir z &&
		test_seq 1 10 >x/d &&
		echo b >z/b &&
		echo c >z/c &&
		git add x z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z y &&
		echo 11 >>x/d &&
		git add x/d &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo eleven >>x/d &&
		git mv x/d z/d &&
		git add z/d &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_success '13b(conflict): messages for transitive rename with conflicted content' '
	test_setup_13b conflict &&
	(
		cd 13b_conflict &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&

		test_grep CONFLICT.*content.*Merge.conflict.in.y/d out &&
		test_grep CONFLICT..file.location.*x/d.renamed.to.z/d.*moved.to.y/d out &&

		git ls-files >paths &&
		! grep z/ paths &&
		grep "y/d" paths &&

		test_path_is_missing z/d &&
		test_path_is_file    y/d
	)
'

test_expect_success '13b(info): messages for transitive rename with conflicted content' '
	test_setup_13b info &&
	(
		cd 13b_info &&

		git reset --hard &&
		git checkout A^0 &&

		test_must_fail git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&

		test_grep CONFLICT.*content.*Merge.conflict.in.y/d out &&
		test_grep Path.updated:.*x/d.renamed.to.z/d.in.B^0.*moving.it.to.y/d out &&

		git ls-files >paths &&
		! grep z/ paths &&
		grep "y/d" paths &&

		test_path_is_missing z/d &&
		test_path_is_file    y/d
	)
'

# Testcase 13c, Rename/rename(1to1) due to directory rename
#   Commit O: z/{b,c},   x/{d,e}
#   Commit A: y/{b,c,d}, x/e
#   Commit B: z/{b,c,d}, x/e
#   Expected: y/{b,c,d}, x/e, with info or conflict messages for d
#             A: renamed x/d -> z/d; B: renamed z/ -> y/ AND renamed x/d to y/d
#             One could argue A had partial knowledge of what was done with
#             d and B had full knowledge, but that's a slippery slope as
#             shown in testcase 13d.

test_setup_13c () {
	git init 13c_$1 &&
	(
		cd 13c_$1 &&

		mkdir x &&
		mkdir z &&
		test_seq 1 10 >x/d &&
		echo e >x/e &&
		echo b >z/b &&
		echo c >z/c &&
		git add x z &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv z y &&
		git mv x/d y/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv x/d z/d &&
		git add z/d &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_success '13c(conflict): messages for rename/rename(1to1) via transitive rename' '
	test_setup_13c conflict &&
	(
		cd 13c_conflict &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&

		test_grep CONFLICT..file.location.*x/d.renamed.to.z/d.*moved.to.y/d out &&

		git ls-files >paths &&
		! grep z/ paths &&
		grep "y/d" paths &&

		test_path_is_missing z/d &&
		test_path_is_file    y/d
	)
'

test_expect_success '13c(info): messages for rename/rename(1to1) via transitive rename' '
	test_setup_13c info &&
	(
		cd 13c_info &&

		git reset --hard &&
		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&

		test_grep Path.updated:.*x/d.renamed.to.z/d.in.B^0.*moving.it.to.y/d out &&

		git ls-files >paths &&
		! grep z/ paths &&
		grep "y/d" paths &&

		test_path_is_missing z/d &&
		test_path_is_file    y/d
	)
'

# Testcase 13d, Rename/rename(1to1) due to directory rename on both sides
#   Commit O: a/{z,y}, b/x,     c/w
#   Commit A: a/z,     b/{y,x}, d/w
#   Commit B: a/z,     d/x,     c/{y,w}
#   Expected: a/z, d/{y,x,w} with no file location conflict for x
#             Easy cases:
#               * z is always in a; so it stays in a.
#               * x starts in b, only modified on one side to move into d/
#               * w starts in c, only modified on one side to move into d/
#             Hard case:
#               * A renames a/y to b/y, and B renames b/->d/ => a/y -> d/y
#               * B renames a/y to c/y, and A renames c/->d/ => a/y -> d/y
#               No conflict in where a/y ends up, so put it in d/y.

test_setup_13d () {
	git init 13d_$1 &&
	(
		cd 13d_$1 &&

		mkdir a &&
		mkdir b &&
		mkdir c &&
		echo z >a/z &&
		echo y >a/y &&
		echo x >b/x &&
		echo w >c/w &&
		git add a b c &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv a/y b/ &&
		git mv c/ d/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		git mv a/y c/ &&
		git mv b/ d/ &&
		test_tick &&
		git commit -m "B"
	)
}

test_expect_success '13d(conflict): messages for rename/rename(1to1) via dual transitive rename' '
	test_setup_13d conflict &&
	(
		cd 13d_conflict &&

		git checkout A^0 &&

		test_must_fail git merge -s recursive B^0 >out 2>err &&

		test_grep CONFLICT..file.location.*a/y.renamed.to.b/y.*moved.to.d/y out &&
		test_grep CONFLICT..file.location.*a/y.renamed.to.c/y.*moved.to.d/y out &&

		git ls-files >paths &&
		! grep b/ paths &&
		! grep c/ paths &&
		grep "d/y" paths &&

		test_path_is_missing b/y &&
		test_path_is_missing c/y &&
		test_path_is_file    d/y
	)
'

test_expect_success '13d(info): messages for rename/rename(1to1) via dual transitive rename' '
	test_setup_13d info &&
	(
		cd 13d_info &&

		git reset --hard &&
		git checkout A^0 &&

		git -c merge.directoryRenames=true merge -s recursive B^0 >out 2>err &&

		test_grep Path.updated.*a/y.renamed.to.b/y.*moving.it.to.d/y out &&
		test_grep Path.updated.*a/y.renamed.to.c/y.*moving.it.to.d/y out &&

		git ls-files >paths &&
		! grep b/ paths &&
		! grep c/ paths &&
		grep "d/y" paths &&

		test_path_is_missing b/y &&
		test_path_is_missing c/y &&
		test_path_is_file    d/y
	)
'

# Testcase 13e, directory rename in virtual merge base
#
# This testcase has a slightly different setup than all the above cases, in
# order to include a recursive case:
#
#      A   C
#      o - o
#     / \ / \
#  O o   X   ?
#     \ / \ /
#      o   o
#      B   D
#
#   Commit O: a/{z,y}
#   Commit A: b/{z,y}
#   Commit B: a/{z,y,x}
#   Commit C: b/{z,y,x}
#   Commit D: b/{z,y}, a/x
#   Expected: b/{z,y,x}  (sort of; see below for why this might not be expected)
#
#   NOTES: 'X' represents a virtual merge base.  With the default of
#          directory rename detection yielding conflicts, merging A and B
#          results in a conflict complaining about whether 'x' should be
#          under 'a/' or 'b/'.  However, when creating the virtual merge
#          base 'X', since virtual merge bases need to be written out as a
#          tree, we cannot have a conflict, so some resolution has to be
#          picked.
#
#          In choosing the right resolution, it's worth noting here that
#          commits C & D are merges of A & B that choose different
#          locations for 'x' (i.e. they resolve the conflict differently),
#          and so it would be nice when merging C & D if git could detect
#          this difference of opinion and report a conflict.  But the only
#          way to do so that I can think of would be to have the virtual
#          merge base place 'x' in some directory other than either 'a/' or
#          'b/', which seems a little weird -- especially since it'd result
#          in a rename/rename(1to2) conflict with a source path that never
#          existed in any version.
#
#          So, for now, when directory rename detection is set to
#          'conflict' just avoid doing directory rename detection at all in
#          the recursive case.  This will not allow us to detect a conflict
#          in the outer merge for this special kind of setup, but it at
#          least avoids hitting a BUG().
#
test_setup_13e () {
	git init 13e &&
	(
		cd 13e &&

		mkdir a &&
		echo z >a/z &&
		echo y >a/y &&
		git add a &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv a/ b/ &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo x >a/x &&
		git add a &&
		test_tick &&
		git commit -m "B" &&

		git branch C A &&
		git branch D B &&

		git checkout C &&
		test_must_fail git -c merge.directoryRenames=conflict merge B &&
		git add b/x &&
		test_tick &&
		git commit -m "C" &&


		git checkout D &&
		test_must_fail git -c merge.directoryRenames=conflict merge A &&
		git add b/x &&
		mkdir a &&
		git mv b/x a/x &&
		test_tick &&
		git commit -m "D"
	)
}

test_expect_success '13e: directory rename detection in recursive case' '
	test_setup_13e &&
	(
		cd 13e &&

		git checkout --quiet D^0 &&

		git -c merge.directoryRenames=conflict merge -s recursive C^0 >out 2>err &&

		test_grep ! CONFLICT out &&
		test_grep ! BUG: err &&
		test_grep ! core.dumped err &&
		test_must_be_empty err &&

		git ls-files >paths &&
		! grep a/x paths &&
		grep b/x paths
	)
'

test_done
