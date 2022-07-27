#!/bin/sh

test_description='git merge-tree --write-tree'

TEST_PASSES_SANITIZE_LEAK=true
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
	git commit -m rename-numbers &&

	git switch --orphan unrelated &&
	>something-else &&
	git add something-else &&
	test_tick &&
	git commit -m first-commit
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
	git ls-files -u >conflicted-file-info &&
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

anonymize_hash() {
	sed -e "s/[0-9a-f]\{40,\}/HASH/g" "$@"
}

test_expect_success 'test conflict notices and such' '
	test_expect_code 1 git merge-tree --write-tree --name-only side1 side2 >out &&
	anonymize_hash out >actual &&

	# Expected results:
	#   "greeting" should merge with conflicts
	#   "numbers" should merge cleanly
	#   "whatever" has *both* a modify/delete and a file/directory conflict
	cat <<-EOF >expect &&
	HASH
	greeting
	whatever~side1

	Auto-merging greeting
	CONFLICT (content): Merge conflict in greeting
	Auto-merging numbers
	CONFLICT (file/directory): directory in the way of whatever from side1; moving it to whatever~side1 instead.
	CONFLICT (modify/delete): whatever~side1 deleted in side2 and modified in side1.  Version side1 of whatever~side1 left in tree.
	EOF

	test_cmp expect actual
'

for opt in $(git merge-tree --git-completion-helper-all)
do
	if test $opt = "--trivial-merge" || test $opt = "--write-tree"
	then
		continue
	fi

	test_expect_success "usage: --trivial-merge is incompatible with $opt" '
		test_expect_code 128 git merge-tree --trivial-merge $opt side1 side2 side3
	'
done

test_expect_success 'Just the conflicted files without the messages' '
	test_expect_code 1 git merge-tree --write-tree --no-messages --name-only side1 side2 >out &&
	anonymize_hash out >actual &&

	test_write_lines HASH greeting whatever~side1 >expect &&

	test_cmp expect actual
'

test_expect_success 'Check conflicted oids and modes without messages' '
	test_expect_code 1 git merge-tree --write-tree --no-messages side1 side2 >out &&
	anonymize_hash out >actual &&

	# Compare the basic output format
	q_to_tab >expect <<-\EOF &&
	HASH
	100644 HASH 1Qgreeting
	100644 HASH 2Qgreeting
	100644 HASH 3Qgreeting
	100644 HASH 1Qwhatever~side1
	100644 HASH 2Qwhatever~side1
	EOF

	test_cmp expect actual &&

	# Check the actual hashes against the `ls-files -u` output too
	tail -n +2 out | sed -e s/side1/HEAD/ >actual &&
	test_cmp conflicted-file-info actual
'

test_expect_success 'NUL terminated conflicted file "lines"' '
	git checkout -b tweak1 side1 &&
	test_write_lines zero 1 2 3 4 5 6 >numbers &&
	git add numbers &&
	git mv numbers "Αυτά μου φαίνονται κινέζικα" &&
	git commit -m "Renamed numbers" &&

	test_expect_code 1 git merge-tree --write-tree -z tweak1 side2 >out &&
	anonymize_hash out >actual &&
	printf "\\n" >>actual &&

	# Expected results:
	#   "greeting" should merge with conflicts
	#   "whatever" has *both* a modify/delete and a file/directory conflict
	#   "Αυτά μου φαίνονται κινέζικα" should have a conflict
	echo HASH | lf_to_nul >expect &&

	q_to_tab <<-EOF | lf_to_nul >>expect &&
	100644 HASH 1Qgreeting
	100644 HASH 2Qgreeting
	100644 HASH 3Qgreeting
	100644 HASH 1Qwhatever~tweak1
	100644 HASH 2Qwhatever~tweak1
	100644 HASH 1QΑυτά μου φαίνονται κινέζικα
	100644 HASH 2QΑυτά μου φαίνονται κινέζικα
	100644 HASH 3QΑυτά μου φαίνονται κινέζικα

	EOF

	q_to_nul <<-EOF >>expect &&
	1QgreetingQAuto-mergingQAuto-merging greeting
	Q1QgreetingQCONFLICT (contents)QCONFLICT (content): Merge conflict in greeting
	Q2Qwhatever~tweak1QwhateverQCONFLICT (file/directory)QCONFLICT (file/directory): directory in the way of whatever from tweak1; moving it to whatever~tweak1 instead.
	Q1Qwhatever~tweak1QCONFLICT (modify/delete)QCONFLICT (modify/delete): whatever~tweak1 deleted in side2 and modified in tweak1.  Version tweak1 of whatever~tweak1 left in tree.
	Q1QΑυτά μου φαίνονται κινέζικαQAuto-mergingQAuto-merging Αυτά μου φαίνονται κινέζικα
	Q1QΑυτά μου φαίνονται κινέζικαQCONFLICT (contents)QCONFLICT (content): Merge conflict in Αυτά μου φαίνονται κινέζικα
	Q
	EOF

	test_cmp expect actual
'

test_expect_success 'error out by default for unrelated histories' '
	test_expect_code 128 git merge-tree --write-tree side1 unrelated 2>error &&

	grep "refusing to merge unrelated histories" error
'

test_expect_success 'can override merge of unrelated histories' '
	git merge-tree --write-tree --allow-unrelated-histories side1 unrelated >tree &&
	TREE=$(cat tree) &&

	git rev-parse side1:numbers side1:greeting side1:whatever unrelated:something-else >expect &&
	git rev-parse $TREE:numbers $TREE:greeting $TREE:whatever $TREE:something-else >actual &&

	test_cmp expect actual
'

test_done
