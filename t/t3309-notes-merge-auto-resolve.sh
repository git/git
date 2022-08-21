#!/bin/sh
#
# Copyright (c) 2010 Johan Herland
#

test_description='Test notes merging with auto-resolving strategies'

. ./test-lib.sh

# Set up a notes merge scenario with all kinds of potential conflicts
test_expect_success 'setup commits' '
	test_commit 1st &&
	test_commit 2nd &&
	test_commit 3rd &&
	test_commit 4th &&
	test_commit 5th &&
	test_commit 6th &&
	test_commit 7th &&
	test_commit 8th &&
	test_commit 9th &&
	test_commit 10th &&
	test_commit 11th &&
	test_commit 12th &&
	test_commit 13th &&
	test_commit 14th &&
	test_commit 15th &&

	test_oid_cache <<-EOF
	hash15a sha1:457a85d6c814ea208550f15fcc48f804ac8dc023
	hash14a sha1:b0c95b954301d69da2bc3723f4cb1680d355937c
	hash13a sha1:5d30216a129eeffa97d9694ffe8c74317a560315
	hash12a sha1:dd161bc149470fd890dd4ab52a4cbd79bbd18c36
	hash11a sha1:7abbc45126d680336fb24294f013a7cdfa3ed545
	hash10a sha1:b8d03e173f67f6505a76f6e00cf93440200dd9be
	hash09a sha1:20c613c835011c48a5abe29170a2402ca6354910
	hash08a sha1:a3daf8a1e4e5dc3409a303ad8481d57bfea7f5d6
	hash07a sha1:897003322b53bc6ca098e9324ee508362347e734
	hash06a sha1:11d97fdebfa5ceee540a3da07bce6fa0222bc082
	hash15b sha1:68b8630d25516028bed862719855b3d6768d7833
	hash14b sha1:5de7ea7ad4f47e7ff91989fb82234634730f75df
	hash13b sha1:3a631fdb6f41b05b55d8f4baf20728ba8f6fccbc
	hash12b sha1:a66055fa82f7a03fe0c02a6aba3287a85abf7c62
	hash05b sha1:154508c7a0bcad82b6fe4b472bc4c26b3bf0825b
	hash04b sha1:e2bfd06a37dd2031684a59a6e2b033e212239c78
	hash03b sha1:5772f42408c0dd6f097a7ca2d24de0e78d1c46b1
	hash15c sha1:9b4b2c61f0615412da3c10f98ff85b57c04ec765
	hash11c sha1:7e3c53503a3db8dd996cb62e37c66e070b44b54d
	hash08c sha1:851e1638784a884c7dd26c5d41f3340f6387413a
	hash05c sha1:99fc34adfc400b95c67b013115e37e31aa9a6d23
	hash02c sha1:283b48219aee9a4105f6cab337e789065c82c2b9
	hash15d sha1:7c4e546efd0fe939f876beb262ece02797880b54
	hash05d sha1:6c841cc36ea496027290967ca96bd2bef54dbb47
	hash15e sha1:d682107b8bf7a7aea1e537a8d5cb6a12b60135f1
	hash05e sha1:357b6ca14c7afd59b7f8b8aaaa6b8b723771135b
	hash15f sha1:6be90240b5f54594203e25d9f2f64b7567175aee
	hash05f sha1:660311d7f78dc53db12ac373a43fca7465381a7e

	hash15a sha256:45b1558e5c1b75f570010fa48aaa67bb2289fcd431b34ad81cb4c8b95f4f872a
	hash14a sha256:6e7af179ea4dd28afdc83ae6912ba0098cdeff764b26a8b750b157dd81749092
	hash13a sha256:7353089961baf555388e1bac68c67c8ea94b08ccbd97532201cf7f6790703052
	hash12a sha256:5863e4521689ee1879ceab3b38d39e93ab5b51ec70aaf6a96ad388fbdedfa25e
	hash11a sha256:82a0ec0338b4ecf8b44304badf4ad38d7469dc41827f38d7ba6c42e3bae3ee98
	hash10a sha256:e84f2564e92de9792c93b8d197262c735d7ccb1de6025cef8759af8f6c3308eb
	hash09a sha256:4dd07764bcec696f195c0ea71ae89e174876403af1637e4642b8f4453fd23028
	hash08a sha256:02132c4546cd88a1d0aa5854dd55da120927f7904ba16afe36fe03e91a622067
	hash07a sha256:369baf7d00c6720efdc10273493555f943051f84a4706fb24caeb353fa4789db
	hash06a sha256:52d32c10353583b2d96a5849b1f1f43c8018e76f3e8ef1b0d46eb5cff7cdefaf
	hash15b sha256:345e6660b345fa174738a31a7a59423c394bdf414804e200bc510c65d971ae96
	hash14b sha256:7653a6596021c52e405cba979eea15a729993e7102b9a61ba4667e34f0ead4a1
	hash13b sha256:0f202a0b6b9690de2349c173dfd766a37e82744f61c14f1c389306f1d69f470b
	hash12b sha256:eb00f219c026136ea6535b16ff8ec3efa510e6bf50098ca041e1a2a1d4b79840
	hash05b sha256:993b2290cd0c24c27c849d99f1904f3b590f77af0f539932734ad05679ac5a2f
	hash04b sha256:c7fba0d6104917fbf35258f40b9fa4fc697cfa992deecd1570a3b08d0a5587a9
	hash03b sha256:7287a2d78a3766c181b08df38951d784b08b72a44f571ed6d855bd0be22c70f6
	hash15c sha256:62316660a22bf97857dc4a16709ec4d93a224e8c9f37d661ef91751e1f4c4166
	hash11c sha256:51c3763de9b08309370adc5036d58debb331980e73097902957c444602551daa
	hash08c sha256:22cf1fa29599898a7218c51135d66ed85d22aad584f77db3305dedce4c3d4798
	hash05c sha256:2508fd86db980f0508893a1c1571bdf3b2ee113dc25ddb1a3a2fb94bd6cd0d58
	hash02c sha256:63bb527e0b4e1c8e1dd0d54dd778ca7c3718689fd6e37c473044cfbcf1cacfdb
	hash15d sha256:667acb4e2d5f8df15e5aea4506dfd16d25bc7feca70fdb0d965a7222f983bb88
	hash05d sha256:09e6b5a6fe666c4a027674b6611a254b7d2528cd211c6b5288d1b4db6c741dfa
	hash15e sha256:e8cbf52f6fcadc6de3c7761e64a89e9fe38d19a03d3e28ef6ca8596d93fc4f3a
	hash05e sha256:cdb1e19f7ba1539f95af51a57edeb88a7ecc97d3c2f52da8c4c86af308595607
	hash15f sha256:29c14cb92da448a923963b8a43994268b19c2e57913de73f3667421fd2c0eeec
	hash05f sha256:14a6e641b2c0a9f398ebac6b4d34afa5efea4c52d2631382f45f8f662266903b
	EOF
'

commit_sha1=$(git rev-parse 1st^{commit})
commit_sha2=$(git rev-parse 2nd^{commit})
commit_sha3=$(git rev-parse 3rd^{commit})
commit_sha4=$(git rev-parse 4th^{commit})
commit_sha5=$(git rev-parse 5th^{commit})
commit_sha6=$(git rev-parse 6th^{commit})
commit_sha7=$(git rev-parse 7th^{commit})
commit_sha8=$(git rev-parse 8th^{commit})
commit_sha9=$(git rev-parse 9th^{commit})
commit_sha10=$(git rev-parse 10th^{commit})
commit_sha11=$(git rev-parse 11th^{commit})
commit_sha12=$(git rev-parse 12th^{commit})
commit_sha13=$(git rev-parse 13th^{commit})
commit_sha14=$(git rev-parse 14th^{commit})
commit_sha15=$(git rev-parse 15th^{commit})

verify_notes () {
	notes_ref="$1"
	suffix="$2"
	git -c core.notesRef="refs/notes/$notes_ref" notes |
		sort >"output_notes_$suffix" &&
	test_cmp "expect_notes_$suffix" "output_notes_$suffix" &&
	git -c core.notesRef="refs/notes/$notes_ref" log --format="%H %s%n%N" \
		>"output_log_$suffix" &&
	test_cmp "expect_log_$suffix" "output_log_$suffix"
}

test_expect_success 'setup merge base (x)' '
	git config core.notesRef refs/notes/x &&
	git notes add -m "x notes on 6th commit" 6th &&
	git notes add -m "x notes on 7th commit" 7th &&
	git notes add -m "x notes on 8th commit" 8th &&
	git notes add -m "x notes on 9th commit" 9th &&
	git notes add -m "x notes on 10th commit" 10th &&
	git notes add -m "x notes on 11th commit" 11th &&
	git notes add -m "x notes on 12th commit" 12th &&
	git notes add -m "x notes on 13th commit" 13th &&
	git notes add -m "x notes on 14th commit" 14th &&
	git notes add -m "x notes on 15th commit" 15th
'

cat <<EOF | sort >expect_notes_x
$(test_oid hash15a) $commit_sha15
$(test_oid hash14a) $commit_sha14
$(test_oid hash13a) $commit_sha13
$(test_oid hash12a) $commit_sha12
$(test_oid hash11a) $commit_sha11
$(test_oid hash10a) $commit_sha10
$(test_oid hash09a) $commit_sha9
$(test_oid hash08a) $commit_sha8
$(test_oid hash07a) $commit_sha7
$(test_oid hash06a) $commit_sha6
EOF

cat >expect_log_x <<EOF
$commit_sha15 15th
x notes on 15th commit

$commit_sha14 14th
x notes on 14th commit

$commit_sha13 13th
x notes on 13th commit

$commit_sha12 12th
x notes on 12th commit

$commit_sha11 11th
x notes on 11th commit

$commit_sha10 10th
x notes on 10th commit

$commit_sha9 9th
x notes on 9th commit

$commit_sha8 8th
x notes on 8th commit

$commit_sha7 7th
x notes on 7th commit

$commit_sha6 6th
x notes on 6th commit

$commit_sha5 5th

$commit_sha4 4th

$commit_sha3 3rd

$commit_sha2 2nd

$commit_sha1 1st

EOF

test_expect_success 'verify state of merge base (x)' 'verify_notes x x'

test_expect_success 'setup local branch (y)' '
	git update-ref refs/notes/y refs/notes/x &&
	git config core.notesRef refs/notes/y &&
	git notes add -f -m "y notes on 3rd commit" 3rd &&
	git notes add -f -m "y notes on 4th commit" 4th &&
	git notes add -f -m "y notes on 5th commit" 5th &&
	git notes remove 6th &&
	git notes remove 7th &&
	git notes remove 8th &&
	git notes add -f -m "y notes on 12th commit" 12th &&
	git notes add -f -m "y notes on 13th commit" 13th &&
	git notes add -f -m "y notes on 14th commit" 14th &&
	git notes add -f -m "y notes on 15th commit" 15th
'

cat <<EOF | sort >expect_notes_y
$(test_oid hash15b) $commit_sha15
$(test_oid hash14b) $commit_sha14
$(test_oid hash13b) $commit_sha13
$(test_oid hash12b) $commit_sha12
$(test_oid hash11a) $commit_sha11
$(test_oid hash10a) $commit_sha10
$(test_oid hash09a) $commit_sha9
$(test_oid hash05b) $commit_sha5
$(test_oid hash04b) $commit_sha4
$(test_oid hash03b) $commit_sha3
EOF

cat >expect_log_y <<EOF
$commit_sha15 15th
y notes on 15th commit

$commit_sha14 14th
y notes on 14th commit

$commit_sha13 13th
y notes on 13th commit

$commit_sha12 12th
y notes on 12th commit

$commit_sha11 11th
x notes on 11th commit

$commit_sha10 10th
x notes on 10th commit

$commit_sha9 9th
x notes on 9th commit

$commit_sha8 8th

$commit_sha7 7th

$commit_sha6 6th

$commit_sha5 5th
y notes on 5th commit

$commit_sha4 4th
y notes on 4th commit

$commit_sha3 3rd
y notes on 3rd commit

$commit_sha2 2nd

$commit_sha1 1st

EOF

test_expect_success 'verify state of local branch (y)' 'verify_notes y y'

test_expect_success 'setup remote branch (z)' '
	git update-ref refs/notes/z refs/notes/x &&
	git config core.notesRef refs/notes/z &&
	git notes add -f -m "z notes on 2nd commit" 2nd &&
	git notes add -f -m "y notes on 4th commit" 4th &&
	git notes add -f -m "z notes on 5th commit" 5th &&
	git notes remove 6th &&
	git notes add -f -m "z notes on 8th commit" 8th &&
	git notes remove 9th &&
	git notes add -f -m "z notes on 11th commit" 11th &&
	git notes remove 12th &&
	git notes add -f -m "y notes on 14th commit" 14th &&
	git notes add -f -m "z notes on 15th commit" 15th
'

cat <<EOF | sort >expect_notes_z
$(test_oid hash15c) $commit_sha15
$(test_oid hash14b) $commit_sha14
$(test_oid hash13a) $commit_sha13
$(test_oid hash11c) $commit_sha11
$(test_oid hash10a) $commit_sha10
$(test_oid hash08c) $commit_sha8
$(test_oid hash07a) $commit_sha7
$(test_oid hash05c) $commit_sha5
$(test_oid hash04b) $commit_sha4
$(test_oid hash02c) $commit_sha2
EOF

cat >expect_log_z <<EOF
$commit_sha15 15th
z notes on 15th commit

$commit_sha14 14th
y notes on 14th commit

$commit_sha13 13th
x notes on 13th commit

$commit_sha12 12th

$commit_sha11 11th
z notes on 11th commit

$commit_sha10 10th
x notes on 10th commit

$commit_sha9 9th

$commit_sha8 8th
z notes on 8th commit

$commit_sha7 7th
x notes on 7th commit

$commit_sha6 6th

$commit_sha5 5th
z notes on 5th commit

$commit_sha4 4th
y notes on 4th commit

$commit_sha3 3rd

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st

EOF

test_expect_success 'verify state of remote branch (z)' 'verify_notes z z'

# At this point, before merging z into y, we have the following status:
#
# commit | base/x  | local/y | remote/z | diff from x to y/z         | result
# -------|---------|---------|----------|----------------------------|-------
# 1st    | [none]  | [none]  | [none]   | unchanged / unchanged      | [none]
# 2nd    | [none]  | [none]  | 283b482  | unchanged / added          | 283b482
# 3rd    | [none]  | 5772f42 | [none]   | added     / unchanged      | 5772f42
# 4th    | [none]  | e2bfd06 | e2bfd06  | added     / added (same)   | e2bfd06
# 5th    | [none]  | 154508c | 99fc34a  | added     / added (diff)   | ???
# 6th    | 11d97fd | [none]  | [none]   | removed   / removed        | [none]
# 7th    | 8970033 | [none]  | 8970033  | removed   / unchanged      | [none]
# 8th    | a3daf8a | [none]  | 851e163  | removed   / changed        | ???
# 9th    | 20c613c | 20c613c | [none]   | unchanged / removed        | [none]
# 10th   | b8d03e1 | b8d03e1 | b8d03e1  | unchanged / unchanged      | b8d03e1
# 11th   | 7abbc45 | 7abbc45 | 7e3c535  | unchanged / changed        | 7e3c535
# 12th   | dd161bc | a66055f | [none]   | changed   / removed        | ???
# 13th   | 5d30216 | 3a631fd | 5d30216  | changed   / unchanged      | 3a631fd
# 14th   | b0c95b9 | 5de7ea7 | 5de7ea7  | changed   / changed (same) | 5de7ea7
# 15th   | 457a85d | 68b8630 | 9b4b2c6  | changed   / changed (diff) | ???

test_expect_success 'merge z into y with invalid strategy => Fail/No changes' '
	git config core.notesRef refs/notes/y &&
	test_must_fail git notes merge --strategy=foo z &&
	# Verify no changes (y)
	verify_notes y y
'

test_expect_success 'merge z into y with invalid configuration option => Fail/No changes' '
	git config core.notesRef refs/notes/y &&
	test_must_fail git -c notes.mergeStrategy="foo" notes merge z &&
	# Verify no changes (y)
	verify_notes y y
'

cat <<EOF | sort >expect_notes_ours
$(test_oid hash15b) $commit_sha15
$(test_oid hash14b) $commit_sha14
$(test_oid hash13b) $commit_sha13
$(test_oid hash12b) $commit_sha12
$(test_oid hash11c) $commit_sha11
$(test_oid hash10a) $commit_sha10
$(test_oid hash05b) $commit_sha5
$(test_oid hash04b) $commit_sha4
$(test_oid hash03b) $commit_sha3
$(test_oid hash02c) $commit_sha2
EOF

cat >expect_log_ours <<EOF
$commit_sha15 15th
y notes on 15th commit

$commit_sha14 14th
y notes on 14th commit

$commit_sha13 13th
y notes on 13th commit

$commit_sha12 12th
y notes on 12th commit

$commit_sha11 11th
z notes on 11th commit

$commit_sha10 10th
x notes on 10th commit

$commit_sha9 9th

$commit_sha8 8th

$commit_sha7 7th

$commit_sha6 6th

$commit_sha5 5th
y notes on 5th commit

$commit_sha4 4th
y notes on 4th commit

$commit_sha3 3rd
y notes on 3rd commit

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st

EOF

test_expect_success 'merge z into y with "ours" strategy => Non-conflicting 3-way merge' '
	git notes merge --strategy=ours z &&
	verify_notes y ours
'

test_expect_success 'reset to pre-merge state (y)' '
	git update-ref refs/notes/y refs/notes/y^1 &&
	# Verify pre-merge state
	verify_notes y y
'

test_expect_success 'merge z into y with "ours" configuration option => Non-conflicting 3-way merge' '
	git -c notes.mergeStrategy="ours" notes merge z &&
	verify_notes y ours
'

test_expect_success 'reset to pre-merge state (y)' '
	git update-ref refs/notes/y refs/notes/y^1 &&
	# Verify pre-merge state
	verify_notes y y
'

test_expect_success 'merge z into y with "ours" per-ref configuration option => Non-conflicting 3-way merge' '
	git -c notes.y.mergeStrategy="ours" notes merge z &&
	verify_notes y ours
'

test_expect_success 'reset to pre-merge state (y)' '
	git update-ref refs/notes/y refs/notes/y^1 &&
	# Verify pre-merge state
	verify_notes y y
'

cat <<EOF | sort >expect_notes_theirs
$(test_oid hash15c) $commit_sha15
$(test_oid hash14b) $commit_sha14
$(test_oid hash13b) $commit_sha13
$(test_oid hash11c) $commit_sha11
$(test_oid hash10a) $commit_sha10
$(test_oid hash08c) $commit_sha8
$(test_oid hash05c) $commit_sha5
$(test_oid hash04b) $commit_sha4
$(test_oid hash03b) $commit_sha3
$(test_oid hash02c) $commit_sha2
EOF

cat >expect_log_theirs <<EOF
$commit_sha15 15th
z notes on 15th commit

$commit_sha14 14th
y notes on 14th commit

$commit_sha13 13th
y notes on 13th commit

$commit_sha12 12th

$commit_sha11 11th
z notes on 11th commit

$commit_sha10 10th
x notes on 10th commit

$commit_sha9 9th

$commit_sha8 8th
z notes on 8th commit

$commit_sha7 7th

$commit_sha6 6th

$commit_sha5 5th
z notes on 5th commit

$commit_sha4 4th
y notes on 4th commit

$commit_sha3 3rd
y notes on 3rd commit

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st

EOF

test_expect_success 'merge z into y with "theirs" strategy => Non-conflicting 3-way merge' '
	git notes merge --strategy=theirs z &&
	verify_notes y theirs
'

test_expect_success 'reset to pre-merge state (y)' '
	git update-ref refs/notes/y refs/notes/y^1 &&
	# Verify pre-merge state
	verify_notes y y
'

test_expect_success 'merge z into y with "theirs" strategy overriding configuration option "ours" => Non-conflicting 3-way merge' '
	git -c notes.mergeStrategy="ours" notes merge --strategy=theirs z &&
	verify_notes y theirs
'

test_expect_success 'reset to pre-merge state (y)' '
	git update-ref refs/notes/y refs/notes/y^1 &&
	# Verify pre-merge state
	verify_notes y y
'

cat <<EOF | sort >expect_notes_union
$(test_oid hash15d) $commit_sha15
$(test_oid hash14b) $commit_sha14
$(test_oid hash13b) $commit_sha13
$(test_oid hash12b) $commit_sha12
$(test_oid hash11c) $commit_sha11
$(test_oid hash10a) $commit_sha10
$(test_oid hash08c) $commit_sha8
$(test_oid hash05d) $commit_sha5
$(test_oid hash04b) $commit_sha4
$(test_oid hash03b) $commit_sha3
$(test_oid hash02c) $commit_sha2
EOF

cat >expect_log_union <<EOF
$commit_sha15 15th
y notes on 15th commit

z notes on 15th commit

$commit_sha14 14th
y notes on 14th commit

$commit_sha13 13th
y notes on 13th commit

$commit_sha12 12th
y notes on 12th commit

$commit_sha11 11th
z notes on 11th commit

$commit_sha10 10th
x notes on 10th commit

$commit_sha9 9th

$commit_sha8 8th
z notes on 8th commit

$commit_sha7 7th

$commit_sha6 6th

$commit_sha5 5th
y notes on 5th commit

z notes on 5th commit

$commit_sha4 4th
y notes on 4th commit

$commit_sha3 3rd
y notes on 3rd commit

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st

EOF

test_expect_success 'merge z into y with "union" strategy => Non-conflicting 3-way merge' '
	git notes merge --strategy=union z &&
	verify_notes y union
'

test_expect_success 'reset to pre-merge state (y)' '
	git update-ref refs/notes/y refs/notes/y^1 &&
	# Verify pre-merge state
	verify_notes y y
'

test_expect_success 'merge z into y with "union" strategy overriding per-ref configuration => Non-conflicting 3-way merge' '
	git -c notes.y.mergeStrategy="theirs" notes merge --strategy=union z &&
	verify_notes y union
'

test_expect_success 'reset to pre-merge state (y)' '
	git update-ref refs/notes/y refs/notes/y^1 &&
	# Verify pre-merge state
	verify_notes y y
'

test_expect_success 'merge z into y with "union" per-ref overriding general configuration => Non-conflicting 3-way merge' '
	git -c notes.y.mergeStrategy="union" -c notes.mergeStrategy="theirs" notes merge z &&
	verify_notes y union
'

test_expect_success 'reset to pre-merge state (y)' '
	git update-ref refs/notes/y refs/notes/y^1 &&
	# Verify pre-merge state
	verify_notes y y
'

test_expect_success 'merge z into y with "manual" per-ref only checks specific ref configuration => Conflicting 3-way merge' '
	test_must_fail git -c notes.z.mergeStrategy="union" notes merge z &&
	git notes merge --abort &&
	verify_notes y y
'

cat <<EOF | sort >expect_notes_union2
$(test_oid hash15e) $commit_sha15
$(test_oid hash14b) $commit_sha14
$(test_oid hash13b) $commit_sha13
$(test_oid hash12b) $commit_sha12
$(test_oid hash11c) $commit_sha11
$(test_oid hash10a) $commit_sha10
$(test_oid hash08c) $commit_sha8
$(test_oid hash05e) $commit_sha5
$(test_oid hash04b) $commit_sha4
$(test_oid hash03b) $commit_sha3
$(test_oid hash02c) $commit_sha2
EOF

cat >expect_log_union2 <<EOF
$commit_sha15 15th
z notes on 15th commit

y notes on 15th commit

$commit_sha14 14th
y notes on 14th commit

$commit_sha13 13th
y notes on 13th commit

$commit_sha12 12th
y notes on 12th commit

$commit_sha11 11th
z notes on 11th commit

$commit_sha10 10th
x notes on 10th commit

$commit_sha9 9th

$commit_sha8 8th
z notes on 8th commit

$commit_sha7 7th

$commit_sha6 6th

$commit_sha5 5th
z notes on 5th commit

y notes on 5th commit

$commit_sha4 4th
y notes on 4th commit

$commit_sha3 3rd
y notes on 3rd commit

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st

EOF

test_expect_success 'merge y into z with "union" strategy => Non-conflicting 3-way merge' '
	git config core.notesRef refs/notes/z &&
	git notes merge --strategy=union y &&
	verify_notes z union2
'

test_expect_success 'reset to pre-merge state (z)' '
	git update-ref refs/notes/z refs/notes/z^1 &&
	# Verify pre-merge state
	verify_notes z z
'

cat <<EOF | sort >expect_notes_cat_sort_uniq
$(test_oid hash15f) $commit_sha15
$(test_oid hash14b) $commit_sha14
$(test_oid hash13b) $commit_sha13
$(test_oid hash12b) $commit_sha12
$(test_oid hash11c) $commit_sha11
$(test_oid hash10a) $commit_sha10
$(test_oid hash08c) $commit_sha8
$(test_oid hash05f) $commit_sha5
$(test_oid hash04b) $commit_sha4
$(test_oid hash03b) $commit_sha3
$(test_oid hash02c) $commit_sha2
EOF

cat >expect_log_cat_sort_uniq <<EOF
$commit_sha15 15th
y notes on 15th commit
z notes on 15th commit

$commit_sha14 14th
y notes on 14th commit

$commit_sha13 13th
y notes on 13th commit

$commit_sha12 12th
y notes on 12th commit

$commit_sha11 11th
z notes on 11th commit

$commit_sha10 10th
x notes on 10th commit

$commit_sha9 9th

$commit_sha8 8th
z notes on 8th commit

$commit_sha7 7th

$commit_sha6 6th

$commit_sha5 5th
y notes on 5th commit
z notes on 5th commit

$commit_sha4 4th
y notes on 4th commit

$commit_sha3 3rd
y notes on 3rd commit

$commit_sha2 2nd
z notes on 2nd commit

$commit_sha1 1st

EOF

test_expect_success 'merge y into z with "cat_sort_uniq" strategy => Non-conflicting 3-way merge' '
	git notes merge --strategy=cat_sort_uniq y &&
	verify_notes z cat_sort_uniq
'

test_expect_success 'reset to pre-merge state (z)' '
	git update-ref refs/notes/z refs/notes/z^1 &&
	# Verify pre-merge state
	verify_notes z z
'

test_expect_success 'merge y into z with "cat_sort_uniq" strategy configuration option => Non-conflicting 3-way merge' '
	git -c notes.mergeStrategy="cat_sort_uniq" notes merge y &&
	verify_notes z cat_sort_uniq
'

test_done
