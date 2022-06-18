#!/bin/sh

test_description='git merge-tree --write-tree'

. ./test-lib.sh

# This test is ort-specific
if test "$GIT_TEST_MERGE_ALGORITHM" != "ort"
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
	git branch side3 &&

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
	git commit -m other-modifications &&

	git checkout side3 &&
	git mv numbers sequence &&
	test_tick &&
	git commit -m rename-numbers
'

test_expect_success 'Clean merge' '
	TREE_OID=$(git merge-tree --write-tree side1 side3) &&
	q_to_tab <<-EOF >expect &&
	100644 blob $(git rev-parse side1:greeting)Qgreeting
	100644 blob $(git rev-parse side1:numbers)Qsequence
	100644 blob $(git rev-parse side1:whatever)Qwhatever
	EOF

	git ls-tree $TREE_OID >actual &&
	test_cmp expect actual
'

test_expect_success 'Content merge and a few conflicts' '
	git checkout side1^0 &&
	test_must_fail git merge side2 &&
	expected_tree=$(git rev-parse AUTO_MERGE) &&

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
	sed -e s/HEAD/side1/ tmp >expect &&
	git show ${actual_tree}:greeting >actual &&
	test_cmp expect actual
'

test_expect_success 'Barf on misspelled option, with exit code other than 0 or 1' '
	# Mis-spell with single "s" instead of double "s"
	test_expect_code 129 git merge-tree --write-tree --mesages FOOBAR side1 side2 2>expect &&

	grep "error: unknown option.*mesages" expect
'

test_expect_success 'Barf on too many arguments' '
	test_expect_code 129 git merge-tree --write-tree side1 side2 invalid 2>expect &&

	grep "^usage: git merge-tree" expect
'

test_done
