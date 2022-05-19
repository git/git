#!/bin/sh

test_description='remerge-diff handling'

. ./test-lib.sh

# This test is ort-specific
if test "${GIT_TEST_MERGE_ALGORITHM}" != ort
then
	skip_all="GIT_TEST_MERGE_ALGORITHM != ort"
	test_done
fi

test_expect_success 'setup basic merges' '
	test_write_lines 1 2 3 4 5 6 7 8 9 >numbers &&
	but add numbers &&
	but cummit -m base &&

	but branch feature_a &&
	but branch feature_b &&
	but branch feature_c &&

	but branch ab_resolution &&
	but branch bc_resolution &&

	but checkout feature_a &&
	test_write_lines 1 2 three 4 5 6 7 eight 9 >numbers &&
	but cummit -a -m change_a &&

	but checkout feature_b &&
	test_write_lines 1 2 tres 4 5 6 7 8 9 >numbers &&
	but cummit -a -m change_b &&

	but checkout feature_c &&
	test_write_lines 1 2 3 4 5 6 7 8 9 10 >numbers &&
	but cummit -a -m change_c &&

	but checkout bc_resolution &&
	but merge --ff-only feature_b &&
	# no conflict
	but merge feature_c &&

	but checkout ab_resolution &&
	but merge --ff-only feature_a &&
	# conflicts!
	test_must_fail but merge feature_b &&
	# Resolve conflict...and make another change elsewhere
	test_write_lines 1 2 drei 4 5 6 7 acht 9 >numbers &&
	but add numbers &&
	but merge --continue
'

test_expect_success 'remerge-diff on a clean merge' '
	but log -1 --oneline bc_resolution >expect &&
	but show --oneline --remerge-diff bc_resolution >actual &&
	test_cmp expect actual
'

test_expect_success 'remerge-diff with both a resolved conflict and an unrelated change' '
	but log -1 --oneline ab_resolution >tmp &&
	cat <<-EOF >>tmp &&
	diff --but a/numbers b/numbers
	remerge CONFLICT (content): Merge conflict in numbers
	index a1fb731..6875544 100644
	--- a/numbers
	+++ b/numbers
	@@ -1,13 +1,9 @@
	 1
	 2
	-<<<<<<< b0ed5cb (change_a)
	-three
	-=======
	-tres
	->>>>>>> 6cd3f82 (change_b)
	+drei
	 4
	 5
	 6
	 7
	-eight
	+acht
	 9
	EOF
	# Hashes above are sha1; rip them out so test works with sha256
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >expect &&

	but show --oneline --remerge-diff ab_resolution >tmp &&
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'setup non-content conflicts' '
	but switch --orphan base &&

	test_write_lines 1 2 3 4 5 6 7 8 9 >numbers &&
	test_write_lines a b c d e f g h i >letters &&
	test_write_lines in the way >content &&
	but add numbers letters content &&
	but cummit -m base &&

	but branch side1 &&
	but branch side2 &&

	but checkout side1 &&
	test_write_lines 1 2 three 4 5 6 7 8 9 >numbers &&
	but mv letters letters_side1 &&
	but mv content file_or_directory &&
	but add numbers &&
	but cummit -m side1 &&

	but checkout side2 &&
	but rm numbers &&
	but mv letters letters_side2 &&
	mkdir file_or_directory &&
	echo hello >file_or_directory/world &&
	but add file_or_directory/world &&
	but cummit -m side2 &&

	but checkout -b resolution side1 &&
	test_must_fail but merge side2 &&
	test_write_lines 1 2 three 4 5 6 7 8 9 >numbers &&
	but add numbers &&
	but add letters_side1 &&
	but rm letters &&
	but rm letters_side2 &&
	but add file_or_directory~HEAD &&
	but mv file_or_directory~HEAD wanted_content &&
	but cummit -m resolved
'

test_expect_success 'remerge-diff with non-content conflicts' '
	but log -1 --oneline resolution >tmp &&
	cat <<-EOF >>tmp &&
	diff --but a/file_or_directory~HASH (side1) b/wanted_content
	similarity index 100%
	rename from file_or_directory~HASH (side1)
	rename to wanted_content
	remerge CONFLICT (file/directory): directory in the way of file_or_directory from HASH (side1); moving it to file_or_directory~HASH (side1) instead.
	diff --but a/letters b/letters
	remerge CONFLICT (rename/rename): letters renamed to letters_side1 in HASH (side1) and to letters_side2 in HASH (side2).
	diff --but a/letters_side2 b/letters_side2
	deleted file mode 100644
	index b236ae5..0000000
	--- a/letters_side2
	+++ /dev/null
	@@ -1,9 +0,0 @@
	-a
	-b
	-c
	-d
	-e
	-f
	-g
	-h
	-i
	diff --but a/numbers b/numbers
	remerge CONFLICT (modify/delete): numbers deleted in HASH (side2) and modified in HASH (side1).  Version HASH (side1) of numbers left in tree.
	EOF
	# We still have some sha1 hashes above; rip them out so test works
	# with sha256
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >expect &&

	but show --oneline --remerge-diff resolution >tmp &&
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'remerge-diff w/ diff-filter=U: all conflict headers, no diff content' '
	but log -1 --oneline resolution >tmp &&
	cat <<-EOF >>tmp &&
	diff --but a/file_or_directory~HASH (side1) b/file_or_directory~HASH (side1)
	remerge CONFLICT (file/directory): directory in the way of file_or_directory from HASH (side1); moving it to file_or_directory~HASH (side1) instead.
	diff --but a/letters b/letters
	remerge CONFLICT (rename/rename): letters renamed to letters_side1 in HASH (side1) and to letters_side2 in HASH (side2).
	diff --but a/numbers b/numbers
	remerge CONFLICT (modify/delete): numbers deleted in HASH (side2) and modified in HASH (side1).  Version HASH (side1) of numbers left in tree.
	EOF
	# We still have some sha1 hashes above; rip them out so test works
	# with sha256
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >expect &&

	but show --oneline --remerge-diff --diff-filter=U resolution >tmp &&
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'remerge-diff w/ diff-filter=R: relevant file + conflict header' '
	but log -1 --oneline resolution >tmp &&
	cat <<-EOF >>tmp &&
	diff --but a/file_or_directory~HASH (side1) b/wanted_content
	similarity index 100%
	rename from file_or_directory~HASH (side1)
	rename to wanted_content
	remerge CONFLICT (file/directory): directory in the way of file_or_directory from HASH (side1); moving it to file_or_directory~HASH (side1) instead.
	EOF
	# We still have some sha1 hashes above; rip them out so test works
	# with sha256
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >expect &&

	but show --oneline --remerge-diff --diff-filter=R resolution >tmp &&
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'remerge-diff w/ pathspec: limits to relevant file including conflict header' '
	but log -1 --oneline resolution >tmp &&
	cat <<-EOF >>tmp &&
	diff --but a/letters b/letters
	remerge CONFLICT (rename/rename): letters renamed to letters_side1 in HASH (side1) and to letters_side2 in HASH (side2).
	diff --but a/letters_side2 b/letters_side2
	deleted file mode 100644
	index b236ae5..0000000
	--- a/letters_side2
	+++ /dev/null
	@@ -1,9 +0,0 @@
	-a
	-b
	-c
	-d
	-e
	-f
	-g
	-h
	-i
	EOF
	# We still have some sha1 hashes above; rip them out so test works
	# with sha256
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >expect &&

	but show --oneline --remerge-diff resolution -- "letters*" >tmp &&
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'setup non-content conflicts' '
	but switch --orphan newbase &&

	test_write_lines 1 2 3 4 5 6 7 8 9 >numbers &&
	but add numbers &&
	but cummit -m base &&

	but branch newside1 &&
	but branch newside2 &&

	but checkout newside1 &&
	test_write_lines 1 2 three 4 5 6 7 8 9 >numbers &&
	but add numbers &&
	but cummit -m side1 &&

	but checkout newside2 &&
	test_write_lines 1 2 drei 4 5 6 7 8 9 >numbers &&
	but add numbers &&
	but cummit -m side2 &&

	but checkout -b newresolution newside1 &&
	test_must_fail but merge newside2 &&
	but checkout --theirs numbers &&
	but add -u numbers &&
	but cummit -m resolved
'

test_expect_success 'remerge-diff turns off history simplification' '
	but log -1 --oneline newresolution >tmp &&
	cat <<-EOF >>tmp &&
	diff --but a/numbers b/numbers
	remerge CONFLICT (content): Merge conflict in numbers
	index 070e9e7..5335e78 100644
	--- a/numbers
	+++ b/numbers
	@@ -1,10 +1,6 @@
	 1
	 2
	-<<<<<<< 96f1e45 (side1)
	-three
	-=======
	 drei
	->>>>>>> 4fd522f (side2)
	 4
	 5
	 6
	EOF
	# We still have some sha1 hashes above; rip them out so test works
	# with sha256
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >expect &&

	but show --oneline --remerge-diff newresolution -- numbers >tmp &&
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >actual &&
	test_cmp expect actual
'

test_done
