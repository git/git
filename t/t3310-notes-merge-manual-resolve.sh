#!/bin/sh
#
# Copyright (c) 2010 Johan Herland
#

test_description='Test notes merging with manual conflict resolution'

. ./test-lib.sh

# Set up a notes merge scenario with different kinds of conflicts
test_expect_success 'setup commits' '
	test_commit 1st &&
	test_commit 2nd &&
	test_commit 3rd &&
	test_commit 4th &&
	test_commit 5th &&

	test_oid_cache <<-EOF
	hash04a sha1:6e8e3febca3c2bb896704335cc4d0c34cb2f8715
	hash03a sha1:e5388c10860456ee60673025345fe2e153eb8cf8
	hash02a sha1:ceefa674873670e7ecd131814d909723cce2b669
	hash04b sha1:e2bfd06a37dd2031684a59a6e2b033e212239c78
	hash03b sha1:5772f42408c0dd6f097a7ca2d24de0e78d1c46b1
	hash01b sha1:b0a6021ec006d07e80e9b20ec9b444cbd9d560d3
	hash04c sha1:cff59c793c20bb49a4e01bc06fb06bad642e0d54
	hash02c sha1:283b48219aee9a4105f6cab337e789065c82c2b9
	hash01c sha1:0a81da8956346e19bcb27a906f04af327e03e31b
	hash04d sha1:00494adecf2d9635a02fa431308d67993f853968
	hash01e sha1:f75d1df88cbfe4258d49852f26cfc83f2ad4494b
	hash04f sha1:021faa20e931fb48986ffc6282b4bb05553ac946
	hash01f sha1:0a59e787e6d688aa6309e56e8c1b89431a0fc1c1
	hash05g sha1:304dfb4325cf243025b9957486eb605a9b51c199

	hash04a	sha256:f18a935e65866345098b3b754071dbf9f3aa3520eb27a7b036b278c5e2f1ed7e
	hash03a	sha256:713035dc94067a64e5fa6e4e1821b7c3bde49a77c7cb3f80eaadefa1ca41b3d2
	hash02a	sha256:f160a67e048b6fa75bec3952184154045076692cf5dccd3da21e3fd34b7a3f0f
	hash04b sha256:c7fba0d6104917fbf35258f40b9fa4fc697cfa992deecd1570a3b08d0a5587a9
	hash03b sha256:7287a2d78a3766c181b08df38951d784b08b72a44f571ed6d855bd0be22c70f6
	hash01b sha256:da96cf778c15d0a2bb76f98b2a62f6c9c01730fa7030e8f08ef0191048e7d620
	hash04c sha256:cb615d2def4b834d5f55b2351df97dc92bee4f5009d285201427f349081c8aca
	hash02c sha256:63bb527e0b4e1c8e1dd0d54dd778ca7c3718689fd6e37c473044cfbcf1cacfdb
	hash01c sha256:5b87237ac1fbae0246256fed9f9a1f077c4140fb7e6444925f8dbfa5ae406cd8
	hash04d sha256:eeddc9f9f6cb3d6b39b861659853f10891dc373e0b6eecb09e03e39b6ce64714
	hash01e sha256:108f521b1a74c2e6d0b52a4eda87e09162bf847f7d190cfce496ee1af0b29a5a
	hash04f sha256:901acda0454502b3bbd281f130c419e6c8de78afcf72a8def8d45ad31462bce4
	hash01f sha256:a2d99d1b8bf23c8af7d9d91368454adc110dfd5cc068a4cebb486ee8f5a1e16c
	hash05g sha256:4fef015b01da8efe929a68e3bb9b8fbad81f53995f097befe8ebc93f12ab98ec
	EOF
'

commit_sha1=$(git rev-parse 1st^{commit})
commit_sha2=$(git rev-parse 2nd^{commit})
commit_sha3=$(git rev-parse 3rd^{commit})
commit_sha4=$(git rev-parse 4th^{commit})
commit_sha5=$(git rev-parse 5th^{commit})

verify_notes () {
	notes_ref="$1"
	git -c core.notesRef="refs/notes/$notes_ref" notes |
		sort >"output_notes_$notes_ref" &&
	test_cmp "expect_notes_$notes_ref" "output_notes_$notes_ref" &&
	git -c core.notesRef="refs/notes/$notes_ref" log --format="%H %s%n%N" \
		>"output_log_$notes_ref" &&
	test_cmp "expect_log_$notes_ref" "output_log_$notes_ref"
}

notes_merge_files_gone () {
	# No .git/NOTES_MERGE_* files left
	{ ls .git/NOTES_MERGE_* >output || :; } &&
	test_must_be_empty output
}

cat <<EOF | sort >expect_notes_x
$(test_oid hash04a) $commit_sha4
$(test_oid hash03a) $commit_sha3
$(test_oid hash02a) $commit_sha2
EOF

cat >expect_log_x <<EOF
$commit_sha5 5th

$commit_sha4 4th
x notes on 4th commit

$commit_sha3 3rd
x notes on 3rd commit

$commit_sha2 2nd
x notes on 2nd commit

$commit_sha1 1st

EOF

test_expect_success 'setup merge base (x)' '
	git config core.notesRef refs/notes/x &&
	git notes add -m "x notes on 2nd commit" 2nd &&
	git notes add -m "x notes on 3rd commit" 3rd &&
	git notes add -m "x notes on 4th commit" 4th &&
	verify_notes x
'

cat <<EOF | sort >expect_notes_y
$(test_oid hash04b) $commit_sha4
$(test_oid hash03b) $commit_sha3
$(test_oid hash01b) $commit_sha1
EOF

cat >expect_log_y <<EOF
$commit_sha5 5th

$commit_sha4 4th
y notes on 4th commit

$commit_sha3 3rd
y notes on 3rd commit

$commit_sha2 2nd

$commit_sha1 1st
y notes on 1st commit

EOF

test_expect_success 'setup local branch (y)' '
	git update-ref refs/notes/y refs/notes/x &&
	git config core.notesRef refs/notes/y &&
	git notes add -f -m "y notes on 1st commit" 1st &&
	git notes remove 2nd &&
	git notes add -f -m "y notes on 3rd commit" 3rd &&
	git notes add -f -m "y notes on 4th commit" 4th &&
	verify_notes y
'

cat <<EOF | sort >expect_notes_z
$(test_oid hash04c) $commit_sha4
$(test_oid hash02c) $commit_sha2
$(test_oid hash01c) $commit_sha1
EOF

cat >expect_log_z <<EOF
$commit_sha5 5th

$commit_sha4 4th
z notes on 4th commit

$commit_sha3 3rd

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st
z notes on 1st commit

EOF

test_expect_success 'setup remote branch (z)' '
	git update-ref refs/notes/z refs/notes/x &&
	git config core.notesRef refs/notes/z &&
	git notes add -f -m "z notes on 1st commit" 1st &&
	git notes add -f -m "z notes on 2nd commit" 2nd &&
	git notes remove 3rd &&
	git notes add -f -m "z notes on 4th commit" 4th &&
	verify_notes z
'

# At this point, before merging z into y, we have the following status:
#
# commit | base/x  | local/y | remote/z | diff from x to y/z
# -------|---------|---------|----------|---------------------------
# 1st    | [none]  | b0a6021 | 0a81da8  | added     / added (diff)
# 2nd    | ceefa67 | [none]  | 283b482  | removed   / changed
# 3rd    | e5388c1 | 5772f42 | [none]   | changed   / removed
# 4th    | 6e8e3fe | e2bfd06 | cff59c7  | changed   / changed (diff)
# 5th    | [none]  | [none]  | [none]   | [none]

cat <<EOF | sort >expect_conflicts
$commit_sha1
$commit_sha2
$commit_sha3
$commit_sha4
EOF

cat >expect_conflict_$commit_sha1 <<EOF
<<<<<<< refs/notes/m
y notes on 1st commit
=======
z notes on 1st commit
>>>>>>> refs/notes/z
EOF

cat >expect_conflict_$commit_sha2 <<EOF
z notes on 2nd commit
EOF

cat >expect_conflict_$commit_sha3 <<EOF
y notes on 3rd commit
EOF

cat >expect_conflict_$commit_sha4 <<EOF
<<<<<<< refs/notes/m
y notes on 4th commit
=======
z notes on 4th commit
>>>>>>> refs/notes/z
EOF

cp expect_notes_y expect_notes_m
cp expect_log_y expect_log_m

git rev-parse refs/notes/y > pre_merge_y
git rev-parse refs/notes/z > pre_merge_z

test_expect_success 'merge z into m (== y) with default ("manual") resolver => Conflicting 3-way merge' '
	git update-ref refs/notes/m refs/notes/y &&
	git config core.notesRef refs/notes/m &&
	test_must_fail git notes merge z >output 2>&1 &&
	# Output should point to where to resolve conflicts
	test_grep "\\.git/NOTES_MERGE_WORKTREE" output &&
	# Inspect merge conflicts
	ls .git/NOTES_MERGE_WORKTREE >output_conflicts &&
	test_cmp expect_conflicts output_conflicts &&
	( for f in $(cat expect_conflicts); do
		test_cmp "expect_conflict_$f" ".git/NOTES_MERGE_WORKTREE/$f" ||
		exit 1
	done ) &&
	# Verify that current notes tree (pre-merge) has not changed (m == y)
	verify_notes y &&
	verify_notes m &&
	test "$(git rev-parse refs/notes/m)" = "$(cat pre_merge_y)"
'

cat <<EOF | sort >expect_notes_z
$(test_oid hash04d) $commit_sha4
$(test_oid hash02c) $commit_sha2
$(test_oid hash01c) $commit_sha1
EOF

cat >expect_log_z <<EOF
$commit_sha5 5th

$commit_sha4 4th
z notes on 4th commit

More z notes on 4th commit

$commit_sha3 3rd

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st
z notes on 1st commit

EOF

test_expect_success 'change notes in z' '
	git notes --ref z append -m "More z notes on 4th commit" 4th &&
	verify_notes z
'

test_expect_success 'cannot do merge w/conflicts when previous merge is unfinished' '
	test -d .git/NOTES_MERGE_WORKTREE &&
	test_must_fail git notes merge z >output 2>&1 &&
	# Output should indicate what is wrong
	test_grep -q "\\.git/NOTES_MERGE_\\* exists" output
'

# Setup non-conflicting merge between x and new notes ref w

cat <<EOF | sort >expect_notes_w
$(test_oid hash02a) $commit_sha2
$(test_oid hash01e) $commit_sha1
EOF

cat >expect_log_w <<EOF
$commit_sha5 5th

$commit_sha4 4th

$commit_sha3 3rd

$commit_sha2 2nd
x notes on 2nd commit

$commit_sha1 1st
w notes on 1st commit

EOF

test_expect_success 'setup unrelated notes ref (w)' '
	git config core.notesRef refs/notes/w &&
	git notes add -m "w notes on 1st commit" 1st &&
	git notes add -m "x notes on 2nd commit" 2nd &&
	verify_notes w
'

cat <<EOF | sort >expect_notes_w
$(test_oid hash04a) $commit_sha4
$(test_oid hash03a) $commit_sha3
$(test_oid hash02a) $commit_sha2
$(test_oid hash01e) $commit_sha1
EOF

cat >expect_log_w <<EOF
$commit_sha5 5th

$commit_sha4 4th
x notes on 4th commit

$commit_sha3 3rd
x notes on 3rd commit

$commit_sha2 2nd
x notes on 2nd commit

$commit_sha1 1st
w notes on 1st commit

EOF

test_expect_success 'can do merge without conflicts even if previous merge is unfinished (x => w)' '
	test -d .git/NOTES_MERGE_WORKTREE &&
	git notes merge x &&
	verify_notes w &&
	# Verify that other notes refs has not changed (x and y)
	verify_notes x &&
	verify_notes y
'

cat <<EOF | sort >expect_notes_m
$(test_oid hash04f) $commit_sha4
$(test_oid hash03b) $commit_sha3
$(test_oid hash02c) $commit_sha2
$(test_oid hash01f) $commit_sha1
EOF

cat >expect_log_m <<EOF
$commit_sha5 5th

$commit_sha4 4th
y and z notes on 4th commit

$commit_sha3 3rd
y notes on 3rd commit

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st
y and z notes on 1st commit

EOF

test_expect_success 'do not allow mixing --commit and --abort' '
	test_must_fail git notes merge --commit --abort
'

test_expect_success 'do not allow mixing --commit and --strategy' '
	test_must_fail git notes merge --commit --strategy theirs
'

test_expect_success 'do not allow mixing --abort and --strategy' '
	test_must_fail git notes merge --abort --strategy theirs
'

test_expect_success 'finalize conflicting merge (z => m)' '
	# Resolve conflicts and finalize merge
	cat >.git/NOTES_MERGE_WORKTREE/$commit_sha1 <<EOF &&
y and z notes on 1st commit
EOF
	cat >.git/NOTES_MERGE_WORKTREE/$commit_sha4 <<EOF &&
y and z notes on 4th commit
EOF
	git notes merge --commit &&
	notes_merge_files_gone &&
	# Merge commit has pre-merge y and pre-merge z as parents
	test "$(git rev-parse refs/notes/m^1)" = "$(cat pre_merge_y)" &&
	test "$(git rev-parse refs/notes/m^2)" = "$(cat pre_merge_z)" &&
	# Merge commit mentions the notes refs merged
	git log -1 --format=%B refs/notes/m > merge_commit_msg &&
	grep -q refs/notes/m merge_commit_msg &&
	grep -q refs/notes/z merge_commit_msg &&
	# Merge commit mentions conflicting notes
	grep -q "Conflicts" merge_commit_msg &&
	( for sha1 in $(cat expect_conflicts); do
		grep -q "$sha1" merge_commit_msg ||
		exit 1
	done ) &&
	# Verify contents of merge result
	verify_notes m &&
	# Verify that other notes refs has not changed (w, x, y and z)
	verify_notes w &&
	verify_notes x &&
	verify_notes y &&
	verify_notes z
'

cat >expect_conflict_$commit_sha4 <<EOF
<<<<<<< refs/notes/m
y notes on 4th commit
=======
z notes on 4th commit

More z notes on 4th commit
>>>>>>> refs/notes/z
EOF

cp expect_notes_y expect_notes_m
cp expect_log_y expect_log_m

git rev-parse refs/notes/y > pre_merge_y
git rev-parse refs/notes/z > pre_merge_z

test_expect_success 'redo merge of z into m (== y) with default ("manual") resolver => Conflicting 3-way merge' '
	git update-ref refs/notes/m refs/notes/y &&
	git config core.notesRef refs/notes/m &&
	test_must_fail git notes merge z >output 2>&1 &&
	# Output should point to where to resolve conflicts
	test_grep "\\.git/NOTES_MERGE_WORKTREE" output &&
	# Inspect merge conflicts
	ls .git/NOTES_MERGE_WORKTREE >output_conflicts &&
	test_cmp expect_conflicts output_conflicts &&
	( for f in $(cat expect_conflicts); do
		test_cmp "expect_conflict_$f" ".git/NOTES_MERGE_WORKTREE/$f" ||
		exit 1
	done ) &&
	# Verify that current notes tree (pre-merge) has not changed (m == y)
	verify_notes y &&
	verify_notes m &&
	test "$(git rev-parse refs/notes/m)" = "$(cat pre_merge_y)"
'

test_expect_success 'abort notes merge' '
	git notes merge --abort &&
	notes_merge_files_gone &&
	# m has not moved (still == y)
	test "$(git rev-parse refs/notes/m)" = "$(cat pre_merge_y)" &&
	# Verify that other notes refs has not changed (w, x, y and z)
	verify_notes w &&
	verify_notes x &&
	verify_notes y &&
	verify_notes z
'

git rev-parse refs/notes/y > pre_merge_y
git rev-parse refs/notes/z > pre_merge_z

test_expect_success 'redo merge of z into m (== y) with default ("manual") resolver => Conflicting 3-way merge' '
	test_must_fail git notes merge z >output 2>&1 &&
	# Output should point to where to resolve conflicts
	test_grep "\\.git/NOTES_MERGE_WORKTREE" output &&
	# Inspect merge conflicts
	ls .git/NOTES_MERGE_WORKTREE >output_conflicts &&
	test_cmp expect_conflicts output_conflicts &&
	( for f in $(cat expect_conflicts); do
		test_cmp "expect_conflict_$f" ".git/NOTES_MERGE_WORKTREE/$f" ||
		exit 1
	done ) &&
	# Verify that current notes tree (pre-merge) has not changed (m == y)
	verify_notes y &&
	verify_notes m &&
	test "$(git rev-parse refs/notes/m)" = "$(cat pre_merge_y)"
'

cat <<EOF | sort >expect_notes_m
$(test_oid hash05g) $commit_sha5
$(test_oid hash02c) $commit_sha2
$(test_oid hash01f) $commit_sha1
EOF

cat >expect_log_m <<EOF
$commit_sha5 5th
new note on 5th commit

$commit_sha4 4th

$commit_sha3 3rd

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st
y and z notes on 1st commit

EOF

test_expect_success 'add + remove notes in finalized merge (z => m)' '
	# Resolve one conflict
	cat >.git/NOTES_MERGE_WORKTREE/$commit_sha1 <<EOF &&
y and z notes on 1st commit
EOF
	# Remove another conflict
	rm .git/NOTES_MERGE_WORKTREE/$commit_sha4 &&
	# Remove a D/F conflict
	rm .git/NOTES_MERGE_WORKTREE/$commit_sha3 &&
	# Add a new note
	echo "new note on 5th commit" > .git/NOTES_MERGE_WORKTREE/$commit_sha5 &&
	# Finalize merge
	git notes merge --commit &&
	notes_merge_files_gone &&
	# Merge commit has pre-merge y and pre-merge z as parents
	test "$(git rev-parse refs/notes/m^1)" = "$(cat pre_merge_y)" &&
	test "$(git rev-parse refs/notes/m^2)" = "$(cat pre_merge_z)" &&
	# Merge commit mentions the notes refs merged
	git log -1 --format=%B refs/notes/m > merge_commit_msg &&
	grep -q refs/notes/m merge_commit_msg &&
	grep -q refs/notes/z merge_commit_msg &&
	# Merge commit mentions conflicting notes
	grep -q "Conflicts" merge_commit_msg &&
	( for sha1 in $(cat expect_conflicts); do
		grep -q "$sha1" merge_commit_msg ||
		exit 1
	done ) &&
	# Verify contents of merge result
	verify_notes m &&
	# Verify that other notes refs has not changed (w, x, y and z)
	verify_notes w &&
	verify_notes x &&
	verify_notes y &&
	verify_notes z
'

cp expect_notes_y expect_notes_m
cp expect_log_y expect_log_m

test_expect_success 'redo merge of z into m (== y) with default ("manual") resolver => Conflicting 3-way merge' '
	git update-ref refs/notes/m refs/notes/y &&
	test_must_fail git notes merge z >output 2>&1 &&
	# Output should point to where to resolve conflicts
	test_grep "\\.git/NOTES_MERGE_WORKTREE" output &&
	# Inspect merge conflicts
	ls .git/NOTES_MERGE_WORKTREE >output_conflicts &&
	test_cmp expect_conflicts output_conflicts &&
	( for f in $(cat expect_conflicts); do
		test_cmp "expect_conflict_$f" ".git/NOTES_MERGE_WORKTREE/$f" ||
		exit 1
	done ) &&
	# Verify that current notes tree (pre-merge) has not changed (m == y)
	verify_notes y &&
	verify_notes m &&
	test "$(git rev-parse refs/notes/m)" = "$(cat pre_merge_y)"
'

cp expect_notes_w expect_notes_m
cp expect_log_w expect_log_m

test_expect_success 'reset notes ref m to somewhere else (w)' '
	git update-ref refs/notes/m refs/notes/w &&
	verify_notes m &&
	test "$(git rev-parse refs/notes/m)" = "$(git rev-parse refs/notes/w)"
'

test_expect_success 'fail to finalize conflicting merge if underlying ref has moved in the meantime (m != NOTES_MERGE_PARTIAL^1)' '
	# Resolve conflicts
	cat >.git/NOTES_MERGE_WORKTREE/$commit_sha1 <<EOF &&
y and z notes on 1st commit
EOF
	cat >.git/NOTES_MERGE_WORKTREE/$commit_sha4 <<EOF &&
y and z notes on 4th commit
EOF
	# Fail to finalize merge
	test_must_fail git notes merge --commit >output 2>&1 &&
	# NOTES_MERGE_* refs and .git/NOTES_MERGE_* state files must remain
	git rev-parse --verify NOTES_MERGE_PARTIAL &&
	git rev-parse --verify NOTES_MERGE_REF &&
	test -f .git/NOTES_MERGE_WORKTREE/$commit_sha1 &&
	test -f .git/NOTES_MERGE_WORKTREE/$commit_sha2 &&
	test -f .git/NOTES_MERGE_WORKTREE/$commit_sha3 &&
	test -f .git/NOTES_MERGE_WORKTREE/$commit_sha4 &&
	# Refs are unchanged
	test "$(git rev-parse refs/notes/m)" = "$(git rev-parse refs/notes/w)" &&
	test "$(git rev-parse refs/notes/y)" = "$(git rev-parse NOTES_MERGE_PARTIAL^1)" &&
	test "$(git rev-parse refs/notes/m)" != "$(git rev-parse NOTES_MERGE_PARTIAL^1)" &&
	# Mention refs/notes/m, and its current and expected value in output
	test_grep -q "refs/notes/m" output &&
	test_grep -q "$(git rev-parse refs/notes/m)" output &&
	test_grep -q "$(git rev-parse NOTES_MERGE_PARTIAL^1)" output &&
	# Verify that other notes refs has not changed (w, x, y and z)
	verify_notes w &&
	verify_notes x &&
	verify_notes y &&
	verify_notes z
'

test_expect_success 'resolve situation by aborting the notes merge' '
	git notes merge --abort &&
	notes_merge_files_gone &&
	# m has not moved (still == w)
	test "$(git rev-parse refs/notes/m)" = "$(git rev-parse refs/notes/w)" &&
	# Verify that other notes refs has not changed (w, x, y and z)
	verify_notes w &&
	verify_notes x &&
	verify_notes y &&
	verify_notes z
'

cat >expect_notes <<EOF
foo
bar
EOF

test_expect_success 'switch cwd before committing notes merge' '
	git notes add -m foo HEAD &&
	git notes --ref=other add -m bar HEAD &&
	test_must_fail git notes merge refs/notes/other &&
	(
		cd .git/NOTES_MERGE_WORKTREE &&
		echo "foo" > $(git rev-parse HEAD) &&
		echo "bar" >> $(git rev-parse HEAD) &&
		git notes merge --commit
	) &&
	git notes show HEAD > actual_notes &&
	test_cmp expect_notes actual_notes
'

test_done
