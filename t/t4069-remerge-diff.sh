#!/bin/sh

test_description='remerge-diff handling'

. ./test-lib.sh

# --remerge-diff uses ort under the hood regardless of setting.  However,
# we set up a file/directory conflict beforehand, and the different backends
# handle the conflict differently, which would require separate code paths
# to resolve.  There's not much point in making the code uglier to do that,
# though, when the real thing we are testing (--remerge-diff) will hardcode
# calls directly into the merge-ort API anyway.  So just force the use of
# ort on the setup too.
GIT_TEST_MERGE_ALGORITHM=ort

test_expect_success 'setup basic merges' '
	test_write_lines 1 2 3 4 5 6 7 8 9 >numbers &&
	git add numbers &&
	git commit -m base &&

	git branch feature_a &&
	git branch feature_b &&
	git branch feature_c &&

	git branch ab_resolution &&
	git branch bc_resolution &&

	git checkout feature_a &&
	test_write_lines 1 2 three 4 5 6 7 eight 9 >numbers &&
	git commit -a -m change_a &&

	git checkout feature_b &&
	test_write_lines 1 2 tres 4 5 6 7 8 9 >numbers &&
	git commit -a -m change_b &&

	git checkout feature_c &&
	test_write_lines 1 2 3 4 5 6 7 8 9 10 >numbers &&
	git commit -a -m change_c &&

	git checkout bc_resolution &&
	git merge --ff-only feature_b &&
	# no conflict
	git merge feature_c &&

	git checkout ab_resolution &&
	git merge --ff-only feature_a &&
	# conflicts!
	test_must_fail git merge feature_b &&
	# Resolve conflict...and make another change elsewhere
	test_write_lines 1 2 drei 4 5 6 7 acht 9 >numbers &&
	git add numbers &&
	git merge --continue
'

test_expect_success 'remerge-diff on a clean merge' '
	git log -1 --oneline bc_resolution >expect &&
	git show --oneline --remerge-diff bc_resolution >actual &&
	test_cmp expect actual
'

test_expect_success 'remerge-diff with both a resolved conflict and an unrelated change' '
	git log -1 --oneline ab_resolution >tmp &&
	cat <<-EOF >>tmp &&
	diff --git a/numbers b/numbers
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

	git show --oneline --remerge-diff ab_resolution >tmp &&
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'setup non-content conflicts' '
	git switch --orphan base &&

	test_write_lines 1 2 3 4 5 6 7 8 9 >numbers &&
	test_write_lines a b c d e f g h i >letters &&
	test_write_lines in the way >content &&
	git add numbers letters content &&
	git commit -m base &&

	git branch side1 &&
	git branch side2 &&

	git checkout side1 &&
	test_write_lines 1 2 three 4 5 6 7 8 9 >numbers &&
	git mv letters letters_side1 &&
	git mv content file_or_directory &&
	git add numbers &&
	git commit -m side1 &&

	git checkout side2 &&
	git rm numbers &&
	git mv letters letters_side2 &&
	mkdir file_or_directory &&
	echo hello >file_or_directory/world &&
	git add file_or_directory/world &&
	git commit -m side2 &&

	git checkout -b resolution side1 &&
	test_must_fail git merge side2 &&
	test_write_lines 1 2 three 4 5 6 7 8 9 >numbers &&
	git add numbers &&
	git add letters_side1 &&
	git rm letters &&
	git rm letters_side2 &&
	git add file_or_directory~HEAD &&
	git mv file_or_directory~HEAD wanted_content &&
	git commit -m resolved
'

test_expect_success 'remerge-diff with non-content conflicts' '
	git log -1 --oneline resolution >tmp &&
	cat <<-EOF >>tmp &&
	diff --git a/file_or_directory~HASH (side1) b/wanted_content
	similarity index 100%
	rename from file_or_directory~HASH (side1)
	rename to wanted_content
	remerge CONFLICT (file/directory): directory in the way of file_or_directory from HASH (side1); moving it to file_or_directory~HASH (side1) instead.
	diff --git a/letters b/letters
	remerge CONFLICT (rename/rename): letters renamed to letters_side1 in HASH (side1) and to letters_side2 in HASH (side2).
	diff --git a/letters_side2 b/letters_side2
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
	diff --git a/numbers b/numbers
	remerge CONFLICT (modify/delete): numbers deleted in HASH (side2) and modified in HASH (side1).  Version HASH (side1) of numbers left in tree.
	EOF
	# We still have some sha1 hashes above; rip them out so test works
	# with sha256
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >expect &&

	git show --oneline --remerge-diff resolution >tmp &&
	sed -e "s/[0-9a-f]\{7,\}/HASH/g" tmp >actual &&
	test_cmp expect actual
'

test_done
