#!/bin/sh
#
# Copyright (c) 2010 Johan Herland
#

test_description='Test merging of notes trees'

. ./test-lib.sh

test_expect_success setup '
	test_commit 1st &&
	test_commit 2nd &&
	test_commit 3rd &&
	test_commit 4th &&
	test_commit 5th &&
	# Create notes on 4 first commits
	git config core.notesRef refs/notes/x &&
	git notes add -m "Notes on 1st commit" 1st &&
	git notes add -m "Notes on 2nd commit" 2nd &&
	git notes add -m "Notes on 3rd commit" 3rd &&
	git notes add -m "Notes on 4th commit" 4th &&
	# Copy notes to remote-notes
	git fetch . refs/notes/*:refs/remote-notes/origin/* &&

	test_oid_cache <<-EOF
	hash4a sha1:5e93d24084d32e1cb61f7070505b9d2530cca987
	hash3a sha1:8366731eeee53787d2bdf8fc1eff7d94757e8da0
	hash2a sha1:eede89064cd42441590d6afec6c37b321ada3389
	hash1a sha1:daa55ffad6cb99bf64226532147ffcaf5ce8bdd1
	hash5b sha1:0f2efbd00262f2fd41dfae33df8765618eeacd99
	hash4b sha1:dec2502dac3ea161543f71930044deff93fa945c
	hash3b sha1:4069cdb399fd45463ec6eef8e051a16a03592d91
	hash2c sha1:d000d30e6ddcfce3a8122c403226a2ce2fd04d9d
	hash1c sha1:43add6bd0c8c0bc871ac7991e0f5573cfba27804
	hash4d sha1:1f257a3a90328557c452f0817d6cc50c89d315d4
	hash3d sha1:05a4927951bcef347f51486575b878b2b60137f2

	hash4a sha256:eef876be1d32ac2e2e42240e0429325cec116e55e88cb2969899fac695aa762f
	hash3a sha256:cf7cd1bc091d7ba4166a86df864110e42087cd893a5ae96bc50d637e0290939d
	hash2a sha256:21ddde7ebce2c285213898cb04deca0fd3209610cf7aaf8222e4e2f45262fae2
	hash1a sha256:f9fe0eda16c6027732ed9d4295689a03abd16f893be69b3dcbf4037ddb191921
	hash5b sha256:20046f2244577797a9e3d3f790ea9eca4d8a6bafb2a5570bcb0e03aa02ce100b
	hash4b sha256:f90563d134c61a95bb88afbd45d48ccc9e919c62aa6fbfcd483302b3e4d8dbcb
	hash3b sha256:988f2aca9f2d87e93e6a73197c2bb99560cc44a2f92d18653968f956f01221e0
	hash2c sha256:84153b777b4d42827a756c6578dcdb59d8ae5d1360b874fb37c430150c825c26
	hash1c sha256:9beb2bc4eef72e4c4087be168a20573e34d993d9ab1883055f23e322afa06567
	hash4d sha256:32de39dc06e679a7abb2d4a55ede7709b3124340a4a90aa305971b1c72ac319d
	hash3d sha256:fa73b20e41cbb7541c4c81d1535016131dbfbeb05bf6a71f6115e9cad31c7af5
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

cat <<EOF | sort >expect_notes_x
$(test_oid hash4a) $commit_sha4
$(test_oid hash3a) $commit_sha3
$(test_oid hash2a) $commit_sha2
$(test_oid hash1a) $commit_sha1
EOF

cat >expect_log_x <<EOF
$commit_sha5 5th

$commit_sha4 4th
Notes on 4th commit

$commit_sha3 3rd
Notes on 3rd commit

$commit_sha2 2nd
Notes on 2nd commit

$commit_sha1 1st
Notes on 1st commit

EOF

test_expect_success 'verify initial notes (x)' '
	verify_notes x
'

cp expect_notes_x expect_notes_y
cp expect_notes_x expect_notes_v
cp expect_log_x expect_log_y
cp expect_log_x expect_log_v

test_expect_success 'fail to merge empty notes ref into empty notes ref (z => y)' '
	test_must_fail git -c "core.notesRef=refs/notes/y" notes merge z
'

test_expect_success 'fail to merge into various non-notes refs' '
	test_must_fail git -c "core.notesRef=refs/notes" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/" notes merge x &&
	git update-ref refs/notes/dir/foo HEAD &&
	test_must_fail git -c "core.notesRef=refs/notes/dir" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/dir/" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/heads/main" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/y:" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/y:foo" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/foo^{bar" notes merge x
'

test_expect_success 'merge non-notes ref into empty notes ref (remote-notes/origin/x => v)' '
	git config core.notesRef refs/notes/v &&
	git notes merge refs/remote-notes/origin/x &&
	verify_notes v &&
	# refs/remote-notes/origin/x and v should point to the same notes commit
	test "$(git rev-parse refs/remote-notes/origin/x)" = "$(git rev-parse refs/notes/v)"
'

test_expect_success 'merge notes into empty notes ref (x => y)' '
	git config core.notesRef refs/notes/y &&
	git notes merge x &&
	verify_notes y &&
	# x and y should point to the same notes commit
	test "$(git rev-parse refs/notes/x)" = "$(git rev-parse refs/notes/y)"
'

test_expect_success 'merge empty notes ref (z => y)' '
	git notes merge z &&
	# y should not change (still == x)
	test "$(git rev-parse refs/notes/x)" = "$(git rev-parse refs/notes/y)"
'

test_expect_success 'change notes on other notes ref (y)' '
	# Not touching notes to 1st commit
	git notes remove 2nd &&
	git notes append -m "More notes on 3rd commit" 3rd &&
	git notes add -f -m "New notes on 4th commit" 4th &&
	git notes add -m "Notes on 5th commit" 5th
'

test_expect_success 'merge previous notes commit (y^ => y) => No-op' '
	pre_state="$(git rev-parse refs/notes/y)" &&
	git notes merge y^ &&
	# y should not move
	test "$pre_state" = "$(git rev-parse refs/notes/y)"
'

cat <<EOF | sort >expect_notes_y
$(test_oid hash5b) $commit_sha5
$(test_oid hash4b) $commit_sha4
$(test_oid hash3b) $commit_sha3
$(test_oid hash1a) $commit_sha1
EOF

cat >expect_log_y <<EOF
$commit_sha5 5th
Notes on 5th commit

$commit_sha4 4th
New notes on 4th commit

$commit_sha3 3rd
Notes on 3rd commit

More notes on 3rd commit

$commit_sha2 2nd

$commit_sha1 1st
Notes on 1st commit

EOF

test_expect_success 'verify changed notes on other notes ref (y)' '
	verify_notes y
'

test_expect_success 'verify unchanged notes on original notes ref (x)' '
	verify_notes x
'

test_expect_success 'merge original notes (x) into changed notes (y) => No-op' '
	git notes merge -vvv x &&
	verify_notes y &&
	verify_notes x
'

cp expect_notes_y expect_notes_x
cp expect_log_y expect_log_x

test_expect_success 'merge changed (y) into original (x) => Fast-forward' '
	git config core.notesRef refs/notes/x &&
	git notes merge y &&
	verify_notes x &&
	verify_notes y &&
	# x and y should point to same the notes commit
	test "$(git rev-parse refs/notes/x)" = "$(git rev-parse refs/notes/y)"
'

test_expect_success 'merge empty notes ref (z => y)' '
	# Prepare empty (but valid) notes ref (z)
	git config core.notesRef refs/notes/z &&
	git notes add -m "foo" &&
	git notes remove &&
	git notes >output_notes_z &&
	test_must_be_empty output_notes_z &&
	# Do the merge (z => y)
	git config core.notesRef refs/notes/y &&
	git notes merge z &&
	verify_notes y &&
	# y should no longer point to the same notes commit as x
	test "$(git rev-parse refs/notes/x)" != "$(git rev-parse refs/notes/y)"
'

cat <<EOF | sort >expect_notes_y
$(test_oid hash5b) $commit_sha5
$(test_oid hash4b) $commit_sha4
$(test_oid hash3b) $commit_sha3
$(test_oid hash2c) $commit_sha2
$(test_oid hash1c) $commit_sha1
EOF

cat >expect_log_y <<EOF
$commit_sha5 5th
Notes on 5th commit

$commit_sha4 4th
New notes on 4th commit

$commit_sha3 3rd
Notes on 3rd commit

More notes on 3rd commit

$commit_sha2 2nd
New notes on 2nd commit

$commit_sha1 1st
Notes on 1st commit

More notes on 1st commit

EOF

test_expect_success 'change notes on other notes ref (y)' '
	# Append to 1st commit notes
	git notes append -m "More notes on 1st commit" 1st &&
	# Add new notes to 2nd commit
	git notes add -m "New notes on 2nd commit" 2nd &&
	verify_notes y
'

cat <<EOF | sort >expect_notes_x
$(test_oid hash5b) $commit_sha5
$(test_oid hash4d) $commit_sha4
$(test_oid hash1a) $commit_sha1
EOF

cat >expect_log_x <<EOF
$commit_sha5 5th
Notes on 5th commit

$commit_sha4 4th
New notes on 4th commit

More notes on 4th commit

$commit_sha3 3rd

$commit_sha2 2nd

$commit_sha1 1st
Notes on 1st commit

EOF

test_expect_success 'change notes on notes ref (x)' '
	git config core.notesRef refs/notes/x &&
	git notes remove 3rd &&
	git notes append -m "More notes on 4th commit" 4th &&
	verify_notes x
'

cat <<EOF | sort >expect_notes_x
$(test_oid hash5b) $commit_sha5
$(test_oid hash4d) $commit_sha4
$(test_oid hash2c) $commit_sha2
$(test_oid hash1c) $commit_sha1
EOF

cat >expect_log_x <<EOF
$commit_sha5 5th
Notes on 5th commit

$commit_sha4 4th
New notes on 4th commit

More notes on 4th commit

$commit_sha3 3rd

$commit_sha2 2nd
New notes on 2nd commit

$commit_sha1 1st
Notes on 1st commit

More notes on 1st commit

EOF

test_expect_success 'merge y into x => Non-conflicting 3-way merge' '
	git notes merge y &&
	verify_notes x &&
	verify_notes y
'

cat <<EOF | sort >expect_notes_w
$(test_oid hash3d) $commit_sha3
$(test_oid hash2c) $commit_sha2
EOF

cat >expect_log_w <<EOF
$commit_sha5 5th

$commit_sha4 4th

$commit_sha3 3rd
New notes on 3rd commit

$commit_sha2 2nd
New notes on 2nd commit

$commit_sha1 1st

EOF

test_expect_success 'create notes on new, separate notes ref (w)' '
	git config core.notesRef refs/notes/w &&
	# Add same note as refs/notes/y on 2nd commit
	git notes add -m "New notes on 2nd commit" 2nd &&
	# Add new note on 3rd commit (non-conflicting)
	git notes add -m "New notes on 3rd commit" 3rd &&
	# Verify state of notes on new, separate notes ref (w)
	verify_notes w
'

cat <<EOF | sort >expect_notes_x
$(test_oid hash5b) $commit_sha5
$(test_oid hash4d) $commit_sha4
$(test_oid hash3d) $commit_sha3
$(test_oid hash2c) $commit_sha2
$(test_oid hash1c) $commit_sha1
EOF

cat >expect_log_x <<EOF
$commit_sha5 5th
Notes on 5th commit

$commit_sha4 4th
New notes on 4th commit

More notes on 4th commit

$commit_sha3 3rd
New notes on 3rd commit

$commit_sha2 2nd
New notes on 2nd commit

$commit_sha1 1st
Notes on 1st commit

More notes on 1st commit

EOF

test_expect_success 'merge w into x => Non-conflicting history-less merge' '
	git config core.notesRef refs/notes/x &&
	git notes merge w &&
	# Verify new state of notes on other notes ref (x)
	verify_notes x &&
	# Also verify that nothing changed on other notes refs (y and w)
	verify_notes y &&
	verify_notes w
'

test_done
