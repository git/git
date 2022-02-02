#!/bin/sh

test_description='git merge-tree --write-tree'

. ./test-lib.sh

# This test is ort-specific
if test "${GIT_TEST_MERGE_ALGORITHM}" != "ort"
then
	skip_all="GIT_TEST_MERGE_ALGORITHM != ort"
	test_done
fi

test_expect_success setup '
	test_write_lines 1 2 3 4 5 >numbers &&
	echo hello >greeting &&
	echo foo >whatever &&
	git add numbers greeting whatever &&
	test_tick &&
	git commit -m initial &&

	git branch side1 &&
	git branch side2 &&

	git checkout side1 &&
	test_write_lines 1 2 3 4 5 6 >numbers &&
	echo hi >greeting &&
	echo bar >whatever &&
	git add numbers greeting whatever &&
	test_tick &&
	git commit -m modify-stuff &&

	git checkout side2 &&
	test_write_lines 0 1 2 3 4 5 >numbers &&
	echo yo >greeting &&
	git rm whatever &&
	mkdir whatever &&
	>whatever/empty &&
	git add numbers greeting whatever/empty &&
	test_tick &&
	git commit -m other-modifications
'

test_expect_success 'Content merge and a few conflicts' '
	git checkout side1^0 &&
	test_must_fail git merge side2 &&
	expected_tree=$(cat .git/AUTO_MERGE) &&

	# We will redo the merge, while we are still in a conflicted state!
	test_when_finished "git reset --hard" &&

	test_expect_code 1 git merge-tree --write-tree side1 side2 >RESULT &&
	actual_tree=$(head -n 1 RESULT) &&

	# Due to differences of e.g. "HEAD" vs "side1", the results will not
	# exactly match.  Dig into individual files.

	# Numbers should have three-way merged cleanly
	test_write_lines 0 1 2 3 4 5 6 >expect &&
	git show ${actual_tree}:numbers >actual &&
	test_cmp expect actual &&

	# whatever and whatever~<branch> should have same HASHES
	git rev-parse ${expected_tree}:whatever ${expected_tree}:whatever~HEAD >expect &&
	git rev-parse ${actual_tree}:whatever ${actual_tree}:whatever~side1 >actual &&
	test_cmp expect actual &&

	# greeting should have a merge conflict
	git show ${expected_tree}:greeting >tmp &&
	cat tmp | sed -e s/HEAD/side1/ >expect &&
	git show ${actual_tree}:greeting >actual &&
	test_cmp expect actual
'

test_expect_success 'Barf on misspelled option, with exit code other than 0 or 1' '
	# Mis-spell with single "s" instead of double "s"
	test_expect_code 129 git merge-tree --write-tree --mesages FOOBAR side1 side2 2>expect &&

	grep "error: unknown option.*mesages" expect
'

test_expect_success 'Barf on too many arguments' '
	test_expect_code 129 git merge-tree --write-tree side1 side2 side3 2>expect &&

	grep "^usage: git merge-tree" expect
'

test_expect_success 'test conflict notices and such' '
	test_expect_code 1 git merge-tree --write-tree side1 side2 >out &&
	sed -e "s/[0-9a-f]\{40,\}/HASH/g" out >actual &&

	# Expected results:
	#   "greeting" should merge with conflicts
	#   "numbers" should merge cleanly
	#   "whatever" has *both* a modify/delete and a file/directory conflict
	cat <<-EOF >expect &&
	HASH

	Auto-merging greeting
	CONFLICT (content): Merge conflict in greeting
	Auto-merging numbers
	CONFLICT (file/directory): directory in the way of whatever from side1; moving it to whatever~side1 instead.
	CONFLICT (modify/delete): whatever~side1 deleted in side2 and modified in side1.  Version side1 of whatever~side1 left in tree.
	EOF

	test_cmp expect actual
'

test_done
