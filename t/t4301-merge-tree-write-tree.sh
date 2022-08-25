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

# directory rename + content conflict
#   Commit O: foo, olddir/{a,b,c}
#   Commit A: modify foo, newdir/{a,b,c}
#   Commit B: modify foo differently & rename foo -> olddir/bar
#   Expected: CONFLICT(content) for for newdir/bar (not olddir/bar or foo)

test_expect_success 'directory rename + content conflict' '
	# Setup
	git init dir-rename-and-content &&
	(
		cd dir-rename-and-content &&
		test_write_lines 1 2 3 4 5 >foo &&
		mkdir olddir &&
		for i in a b c; do echo $i >olddir/$i; done
		git add foo olddir &&
		git commit -m "original" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines 1 2 3 4 5 6 >foo &&
		git add foo &&
		git mv olddir newdir &&
		git commit -m "Modify foo, rename olddir to newdir" &&

		git checkout B &&
		test_write_lines 1 2 3 4 5 six >foo &&
		git add foo &&
		git mv foo olddir/bar &&
		git commit -m "Modify foo & rename foo -> olddir/bar"
	) &&
	# Testing
	(
		cd dir-rename-and-content &&

		test_expect_code 1 \
			git merge-tree -z A^0 B^0 >out &&
		printf "\\n" >>out &&
		anonymize_hash out >actual &&
		q_to_tab <<-\EOF | lf_to_nul >expect &&
		HASH
		100644 HASH 1Qnewdir/bar
		100644 HASH 2Qnewdir/bar
		100644 HASH 3Qnewdir/bar
		EOF

		q_to_nul <<-EOF >>expect &&
		Q2Qnewdir/barQolddir/barQCONFLICT (directory rename suggested)QCONFLICT (file location): foo renamed to olddir/bar in B^0, inside a directory that was renamed in A^0, suggesting it should perhaps be moved to newdir/bar.
		Q1Qnewdir/barQAuto-mergingQAuto-merging newdir/bar
		Q1Qnewdir/barQCONFLICT (contents)QCONFLICT (content): Merge conflict in newdir/bar
		Q
		EOF
		test_cmp expect actual
	)
'

# rename/delete + modify/delete handling
#   Commit O: foo
#   Commit A: modify foo + rename to bar
#   Commit B: delete foo
#   Expected: CONFLICT(rename/delete) + CONFLICT(modify/delete)

test_expect_success 'rename/delete handling' '
	# Setup
	git init rename-delete &&
	(
		cd rename-delete &&
		test_write_lines 1 2 3 4 5 >foo &&
		git add foo &&
		git commit -m "original" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines 1 2 3 4 5 6 >foo &&
		git add foo &&
		git mv foo bar &&
		git commit -m "Modify foo, rename to bar" &&

		git checkout B &&
		git rm foo &&
		git commit -m "remove foo"
	) &&
	# Testing
	(
		cd rename-delete &&

		test_expect_code 1 \
			git merge-tree -z A^0 B^0 >out &&
		printf "\\n" >>out &&
		anonymize_hash out >actual &&
		q_to_tab <<-\EOF | lf_to_nul >expect &&
		HASH
		100644 HASH 1Qbar
		100644 HASH 2Qbar
		EOF

		q_to_nul <<-EOF >>expect &&
		Q2QbarQfooQCONFLICT (rename/delete)QCONFLICT (rename/delete): foo renamed to bar in A^0, but deleted in B^0.
		Q1QbarQCONFLICT (modify/delete)QCONFLICT (modify/delete): bar deleted in B^0 and modified in A^0.  Version A^0 of bar left in tree.
		Q
		EOF
		test_cmp expect actual
	)
'

# rename/add handling
#   Commit O: foo
#   Commit A: modify foo, add different bar
#   Commit B: modify & rename foo->bar
#   Expected: CONFLICT(add/add) [via rename collide] for bar

test_expect_success 'rename/add handling' '
	# Setup
	git init rename-add &&
	(
		cd rename-add &&
		test_write_lines original 1 2 3 4 5 >foo &&
		git add foo &&
		git commit -m "original" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines 1 2 3 4 5 >foo &&
		echo "different file" >bar &&
		git add foo bar &&
		git commit -m "Modify foo, add bar" &&

		git checkout B &&
		test_write_lines original 1 2 3 4 5 6 >foo &&
		git add foo &&
		git mv foo bar &&
		git commit -m "rename foo to bar"
	) &&
	# Testing
	(
		cd rename-add &&

		test_expect_code 1 \
			git merge-tree -z A^0 B^0 >out &&
		printf "\\n" >>out &&

		#
		# First, check that the bar that appears at stage 3 does not
		# correspond to an individual blob anywhere in history
		#
		hash=$(cat out | tr "\0" "\n" | head -n 3 | grep 3.bar | cut -f 2 -d " ") &&
		git rev-list --objects --all >all_blobs &&
		! grep $hash all_blobs &&

		#
		# Second, check anonymized hash output against expectation
		#
		anonymize_hash out >actual &&
		q_to_tab <<-\EOF | lf_to_nul >expect &&
		HASH
		100644 HASH 2Qbar
		100644 HASH 3Qbar
		EOF

		q_to_nul <<-EOF >>expect &&
		Q1QbarQAuto-mergingQAuto-merging bar
		Q1QbarQCONFLICT (contents)QCONFLICT (add/add): Merge conflict in bar
		Q1QfooQAuto-mergingQAuto-merging foo
		Q
		EOF
		test_cmp expect actual
	)
'

# rename/add, where add is a mode conflict
#   Commit O: foo
#   Commit A: modify foo, add symlink bar
#   Commit B: modify & rename foo->bar
#   Expected: CONFLICT(distinct modes) for bar

test_expect_success SYMLINKS 'rename/add, where add is a mode conflict' '
	# Setup
	git init rename-add-symlink &&
	(
		cd rename-add-symlink &&
		test_write_lines original 1 2 3 4 5 >foo &&
		git add foo &&
		git commit -m "original" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines 1 2 3 4 5 >foo &&
		ln -s foo bar &&
		git add foo bar &&
		git commit -m "Modify foo, add symlink bar" &&

		git checkout B &&
		test_write_lines original 1 2 3 4 5 6 >foo &&
		git add foo &&
		git mv foo bar &&
		git commit -m "rename foo to bar"
	) &&
	# Testing
	(
		cd rename-add-symlink &&

		test_expect_code 1 \
			git merge-tree -z A^0 B^0 >out &&
		printf "\\n" >>out &&

		#
		# First, check that the bar that appears at stage 3 does not
		# correspond to an individual blob anywhere in history
		#
		hash=$(cat out | tr "\0" "\n" | head -n 3 | grep 3.bar | cut -f 2 -d " ") &&
		git rev-list --objects --all >all_blobs &&
		! grep $hash all_blobs &&

		#
		# Second, check anonymized hash output against expectation
		#
		anonymize_hash out >actual &&
		q_to_tab <<-\EOF | lf_to_nul >expect &&
		HASH
		120000 HASH 2Qbar
		100644 HASH 3Qbar~B^0
		EOF

		q_to_nul <<-EOF >>expect &&
		Q2QbarQbar~B^0QCONFLICT (distinct modes)QCONFLICT (distinct types): bar had different types on each side; renamed one of them so each can be recorded somewhere.
		Q1QfooQAuto-mergingQAuto-merging foo
		Q
		EOF
		test_cmp expect actual
	)
'

# rename/rename(1to2) + content conflict handling
#   Commit O: foo
#   Commit A: modify foo & rename to bar
#   Commit B: modify foo & rename to baz
#   Expected: CONFLICT(rename/rename)

test_expect_success 'rename/rename + content conflict' '
	# Setup
	git init rr-plus-content &&
	(
		cd rr-plus-content &&
		test_write_lines 1 2 3 4 5 >foo &&
		git add foo &&
		git commit -m "original" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_write_lines 1 2 3 4 5 six >foo &&
		git add foo &&
		git mv foo bar &&
		git commit -m "Modify foo + rename to bar" &&

		git checkout B &&
		test_write_lines 1 2 3 4 5 6 >foo &&
		git add foo &&
		git mv foo baz &&
		git commit -m "Modify foo + rename to baz"
	) &&
	# Testing
	(
		cd rr-plus-content &&

		test_expect_code 1 \
			git merge-tree -z A^0 B^0 >out &&
		printf "\\n" >>out &&
		anonymize_hash out >actual &&
		q_to_tab <<-\EOF | lf_to_nul >expect &&
		HASH
		100644 HASH 2Qbar
		100644 HASH 3Qbaz
		100644 HASH 1Qfoo
		EOF

		q_to_nul <<-EOF >>expect &&
		Q1QfooQAuto-mergingQAuto-merging foo
		Q3QfooQbarQbazQCONFLICT (rename/rename)QCONFLICT (rename/rename): foo renamed to bar in A^0 and to baz in B^0.
		Q
		EOF
		test_cmp expect actual
	)
'

# rename/add/delete
#   Commit O: foo
#   Commit A: rm foo, add different bar
#   Commit B: rename foo->bar
#   Expected: CONFLICT (rename/delete), CONFLICT(add/add) [via rename collide]
#             for bar

test_expect_success 'rename/add/delete conflict' '
	# Setup
	git init rad &&
	(
		cd rad &&
		echo "original file" >foo &&
		git add foo &&
		git commit -m "original" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git rm foo &&
		echo "different file" >bar &&
		git add bar &&
		git commit -m "Remove foo, add bar" &&

		git checkout B &&
		git mv foo bar &&
		git commit -m "rename foo to bar"
	) &&
	# Testing
	(
		cd rad &&

		test_expect_code 1 \
			git merge-tree -z B^0 A^0 >out &&
		printf "\\n" >>out &&
		anonymize_hash out >actual &&

		q_to_tab <<-\EOF | lf_to_nul >expect &&
		HASH
		100644 HASH 2Qbar
		100644 HASH 3Qbar

		EOF

		q_to_nul <<-EOF >>expect &&
		2QbarQfooQCONFLICT (rename/delete)QCONFLICT (rename/delete): foo renamed to bar in B^0, but deleted in A^0.
		Q1QbarQAuto-mergingQAuto-merging bar
		Q1QbarQCONFLICT (contents)QCONFLICT (add/add): Merge conflict in bar
		Q
		EOF
		test_cmp expect actual
	)
'

# rename/rename(2to1)/delete/delete
#   Commit O: foo, bar
#   Commit A: rename foo->baz, rm bar
#   Commit B: rename bar->baz, rm foo
#   Expected: 2x CONFLICT (rename/delete), CONFLICT (add/add) via colliding
#             renames for baz

test_expect_success 'rename/rename(2to1)/delete/delete conflict' '
	# Setup
	git init rrdd &&
	(
		cd rrdd &&
		echo foo >foo &&
		echo bar >bar &&
		git add foo bar &&
		git commit -m O &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git mv foo baz &&
		git rm bar &&
		git commit -m "Rename foo, remove bar" &&

		git checkout B &&
		git mv bar baz &&
		git rm foo &&
		git commit -m "Rename bar, remove foo"
	) &&
	# Testing
	(
		cd rrdd &&

		test_expect_code 1 \
			git merge-tree -z A^0 B^0 >out &&
		printf "\\n" >>out &&
		anonymize_hash out >actual &&

		q_to_tab <<-\EOF | lf_to_nul >expect &&
		HASH
		100644 HASH 2Qbaz
		100644 HASH 3Qbaz

		EOF

		q_to_nul <<-EOF >>expect &&
		2QbazQbarQCONFLICT (rename/delete)QCONFLICT (rename/delete): bar renamed to baz in B^0, but deleted in A^0.
		Q2QbazQfooQCONFLICT (rename/delete)QCONFLICT (rename/delete): foo renamed to baz in A^0, but deleted in B^0.
		Q1QbazQAuto-mergingQAuto-merging baz
		Q1QbazQCONFLICT (contents)QCONFLICT (add/add): Merge conflict in baz
		Q
		EOF
		test_cmp expect actual
	)
'

# mod6: chains of rename/rename(1to2) + add/add via colliding renames
#   Commit O: one,      three,       five
#   Commit A: one->two, three->four, five->six
#   Commit B: one->six, three->two,  five->four
#   Expected: three CONFLICT(rename/rename) messages + three CONFLICT(add/add)
#             messages; each path in two of the multi-way merged contents
#             found in two, four, six

test_expect_success 'mod6: chains of rename/rename(1to2) and add/add via colliding renames' '
	# Setup
	git init mod6 &&
	(
		cd mod6 &&
		test_seq 11 19 >one &&
		test_seq 31 39 >three &&
		test_seq 51 59 >five &&
		git add . &&
		test_tick &&
		git commit -m "O" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		test_seq 10 19 >one &&
		echo 40        >>three &&
		git add one three &&
		git mv  one   two  &&
		git mv  three four &&
		git mv  five  six  &&
		test_tick &&
		git commit -m "A" &&

		git checkout B &&
		echo 20    >>one       &&
		echo forty >>three     &&
		echo 60    >>five      &&
		git add one three five &&
		git mv  one   six  &&
		git mv  three two  &&
		git mv  five  four &&
		test_tick &&
		git commit -m "B"
	) &&
	# Testing
	(
		cd mod6 &&

		test_expect_code 1 \
			git merge-tree -z A^0 B^0 >out &&
		printf "\\n" >>out &&

		#
		# First, check that some of the hashes that appear as stage
		# conflict entries do not appear as individual blobs anywhere
		# in history.
		#
		hash1=$(cat out | tr "\0" "\n" | head | grep 2.four | cut -f 2 -d " ") &&
		hash2=$(cat out | tr "\0" "\n" | head | grep 3.two | cut -f 2 -d " ") &&
		git rev-list --objects --all >all_blobs &&
		! grep $hash1 all_blobs &&
		! grep $hash2 all_blobs &&

		#
		# Now compare anonymized hash output with expectation
		#
		anonymize_hash out >actual &&
		q_to_tab <<-\EOF | lf_to_nul >expect &&
		HASH
		100644 HASH 1Qfive
		100644 HASH 2Qfour
		100644 HASH 3Qfour
		100644 HASH 1Qone
		100644 HASH 2Qsix
		100644 HASH 3Qsix
		100644 HASH 1Qthree
		100644 HASH 2Qtwo
		100644 HASH 3Qtwo

		EOF

		q_to_nul <<-EOF >>expect &&
		3QfiveQsixQfourQCONFLICT (rename/rename)QCONFLICT (rename/rename): five renamed to six in A^0 and to four in B^0.
		Q1QfourQAuto-mergingQAuto-merging four
		Q1QfourQCONFLICT (contents)QCONFLICT (add/add): Merge conflict in four
		Q1QoneQAuto-mergingQAuto-merging one
		Q3QoneQtwoQsixQCONFLICT (rename/rename)QCONFLICT (rename/rename): one renamed to two in A^0 and to six in B^0.
		Q1QsixQAuto-mergingQAuto-merging six
		Q1QsixQCONFLICT (contents)QCONFLICT (add/add): Merge conflict in six
		Q1QthreeQAuto-mergingQAuto-merging three
		Q3QthreeQfourQtwoQCONFLICT (rename/rename)QCONFLICT (rename/rename): three renamed to four in A^0 and to two in B^0.
		Q1QtwoQAuto-mergingQAuto-merging two
		Q1QtwoQCONFLICT (contents)QCONFLICT (add/add): Merge conflict in two
		Q
		EOF
		test_cmp expect actual
	)
'

# directory rename + rename/delete + modify/delete + directory/file conflict
#   Commit O: foo, olddir/{a,b,c}
#   Commit A: delete foo, rename olddir/ -> newdir/, add newdir/bar/file
#   Commit B: modify foo & rename foo -> olddir/bar
#   Expected: CONFLICT(content) for for newdir/bar (not olddir/bar or foo)

test_expect_success 'directory rename + rename/delete + modify/delete + directory/file conflict' '
	# Setup
	git init 4-stacked-conflict &&
	(
		cd 4-stacked-conflict &&
		test_write_lines 1 2 3 4 5 >foo &&
		mkdir olddir &&
		for i in a b c; do echo $i >olddir/$i; done
		git add foo olddir &&
		git commit -m "original" &&

		git branch O &&
		git branch A &&
		git branch B &&

		git checkout A &&
		git rm foo &&
		git mv olddir newdir &&
		mkdir newdir/bar &&
		>newdir/bar/file &&
		git add newdir/bar/file &&
		git commit -m "rm foo, olddir/ -> newdir/, + newdir/bar/file" &&

		git checkout B &&
		test_write_lines 1 2 3 4 5 6 >foo &&
		git add foo &&
		git mv foo olddir/bar &&
		git commit -m "Modify foo & rename foo -> olddir/bar"
	) &&
	# Testing
	(
		cd 4-stacked-conflict &&

		test_expect_code 1 \
			git merge-tree -z A^0 B^0 >out &&
		printf "\\n" >>out &&
		anonymize_hash out >actual &&

		q_to_tab <<-\EOF | lf_to_nul >expect &&
		HASH
		100644 HASH 1Qnewdir/bar~B^0
		100644 HASH 3Qnewdir/bar~B^0
		EOF

		q_to_nul <<-EOF >>expect &&
		Q2Qnewdir/barQolddir/barQCONFLICT (directory rename suggested)QCONFLICT (file location): foo renamed to olddir/bar in B^0, inside a directory that was renamed in A^0, suggesting it should perhaps be moved to newdir/bar.
		Q2Qnewdir/barQfooQCONFLICT (rename/delete)QCONFLICT (rename/delete): foo renamed to newdir/bar in B^0, but deleted in A^0.
		Q2Qnewdir/bar~B^0Qnewdir/barQCONFLICT (file/directory)QCONFLICT (file/directory): directory in the way of newdir/bar from B^0; moving it to newdir/bar~B^0 instead.
		Q1Qnewdir/bar~B^0QCONFLICT (modify/delete)QCONFLICT (modify/delete): newdir/bar~B^0 deleted in A^0 and modified in B^0.  Version B^0 of newdir/bar~B^0 left in tree.
		Q
		EOF
		test_cmp expect actual
	)
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
