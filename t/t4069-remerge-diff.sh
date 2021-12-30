#!/bin/sh

test_description='remerge-diff handling'

. ./test-lib.sh

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

test_done
