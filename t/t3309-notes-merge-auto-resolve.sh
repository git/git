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
	test_commit 15th
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
457a85d6c814ea208550f15fcc48f804ac8dc023 $commit_sha15
b0c95b954301d69da2bc3723f4cb1680d355937c $commit_sha14
5d30216a129eeffa97d9694ffe8c74317a560315 $commit_sha13
dd161bc149470fd890dd4ab52a4cbd79bbd18c36 $commit_sha12
7abbc45126d680336fb24294f013a7cdfa3ed545 $commit_sha11
b8d03e173f67f6505a76f6e00cf93440200dd9be $commit_sha10
20c613c835011c48a5abe29170a2402ca6354910 $commit_sha9
a3daf8a1e4e5dc3409a303ad8481d57bfea7f5d6 $commit_sha8
897003322b53bc6ca098e9324ee508362347e734 $commit_sha7
11d97fdebfa5ceee540a3da07bce6fa0222bc082 $commit_sha6
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
68b8630d25516028bed862719855b3d6768d7833 $commit_sha15
5de7ea7ad4f47e7ff91989fb82234634730f75df $commit_sha14
3a631fdb6f41b05b55d8f4baf20728ba8f6fccbc $commit_sha13
a66055fa82f7a03fe0c02a6aba3287a85abf7c62 $commit_sha12
7abbc45126d680336fb24294f013a7cdfa3ed545 $commit_sha11
b8d03e173f67f6505a76f6e00cf93440200dd9be $commit_sha10
20c613c835011c48a5abe29170a2402ca6354910 $commit_sha9
154508c7a0bcad82b6fe4b472bc4c26b3bf0825b $commit_sha5
e2bfd06a37dd2031684a59a6e2b033e212239c78 $commit_sha4
5772f42408c0dd6f097a7ca2d24de0e78d1c46b1 $commit_sha3
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
9b4b2c61f0615412da3c10f98ff85b57c04ec765 $commit_sha15
5de7ea7ad4f47e7ff91989fb82234634730f75df $commit_sha14
5d30216a129eeffa97d9694ffe8c74317a560315 $commit_sha13
7e3c53503a3db8dd996cb62e37c66e070b44b54d $commit_sha11
b8d03e173f67f6505a76f6e00cf93440200dd9be $commit_sha10
851e1638784a884c7dd26c5d41f3340f6387413a $commit_sha8
897003322b53bc6ca098e9324ee508362347e734 $commit_sha7
99fc34adfc400b95c67b013115e37e31aa9a6d23 $commit_sha5
e2bfd06a37dd2031684a59a6e2b033e212239c78 $commit_sha4
283b48219aee9a4105f6cab337e789065c82c2b9 $commit_sha2
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
68b8630d25516028bed862719855b3d6768d7833 $commit_sha15
5de7ea7ad4f47e7ff91989fb82234634730f75df $commit_sha14
3a631fdb6f41b05b55d8f4baf20728ba8f6fccbc $commit_sha13
a66055fa82f7a03fe0c02a6aba3287a85abf7c62 $commit_sha12
7e3c53503a3db8dd996cb62e37c66e070b44b54d $commit_sha11
b8d03e173f67f6505a76f6e00cf93440200dd9be $commit_sha10
154508c7a0bcad82b6fe4b472bc4c26b3bf0825b $commit_sha5
e2bfd06a37dd2031684a59a6e2b033e212239c78 $commit_sha4
5772f42408c0dd6f097a7ca2d24de0e78d1c46b1 $commit_sha3
283b48219aee9a4105f6cab337e789065c82c2b9 $commit_sha2
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
9b4b2c61f0615412da3c10f98ff85b57c04ec765 $commit_sha15
5de7ea7ad4f47e7ff91989fb82234634730f75df $commit_sha14
3a631fdb6f41b05b55d8f4baf20728ba8f6fccbc $commit_sha13
7e3c53503a3db8dd996cb62e37c66e070b44b54d $commit_sha11
b8d03e173f67f6505a76f6e00cf93440200dd9be $commit_sha10
851e1638784a884c7dd26c5d41f3340f6387413a $commit_sha8
99fc34adfc400b95c67b013115e37e31aa9a6d23 $commit_sha5
e2bfd06a37dd2031684a59a6e2b033e212239c78 $commit_sha4
5772f42408c0dd6f097a7ca2d24de0e78d1c46b1 $commit_sha3
283b48219aee9a4105f6cab337e789065c82c2b9 $commit_sha2
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
7c4e546efd0fe939f876beb262ece02797880b54 $commit_sha15
5de7ea7ad4f47e7ff91989fb82234634730f75df $commit_sha14
3a631fdb6f41b05b55d8f4baf20728ba8f6fccbc $commit_sha13
a66055fa82f7a03fe0c02a6aba3287a85abf7c62 $commit_sha12
7e3c53503a3db8dd996cb62e37c66e070b44b54d $commit_sha11
b8d03e173f67f6505a76f6e00cf93440200dd9be $commit_sha10
851e1638784a884c7dd26c5d41f3340f6387413a $commit_sha8
6c841cc36ea496027290967ca96bd2bef54dbb47 $commit_sha5
e2bfd06a37dd2031684a59a6e2b033e212239c78 $commit_sha4
5772f42408c0dd6f097a7ca2d24de0e78d1c46b1 $commit_sha3
283b48219aee9a4105f6cab337e789065c82c2b9 $commit_sha2
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
d682107b8bf7a7aea1e537a8d5cb6a12b60135f1 $commit_sha15
5de7ea7ad4f47e7ff91989fb82234634730f75df $commit_sha14
3a631fdb6f41b05b55d8f4baf20728ba8f6fccbc $commit_sha13
a66055fa82f7a03fe0c02a6aba3287a85abf7c62 $commit_sha12
7e3c53503a3db8dd996cb62e37c66e070b44b54d $commit_sha11
b8d03e173f67f6505a76f6e00cf93440200dd9be $commit_sha10
851e1638784a884c7dd26c5d41f3340f6387413a $commit_sha8
357b6ca14c7afd59b7f8b8aaaa6b8b723771135b $commit_sha5
e2bfd06a37dd2031684a59a6e2b033e212239c78 $commit_sha4
5772f42408c0dd6f097a7ca2d24de0e78d1c46b1 $commit_sha3
283b48219aee9a4105f6cab337e789065c82c2b9 $commit_sha2
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
6be90240b5f54594203e25d9f2f64b7567175aee $commit_sha15
5de7ea7ad4f47e7ff91989fb82234634730f75df $commit_sha14
3a631fdb6f41b05b55d8f4baf20728ba8f6fccbc $commit_sha13
a66055fa82f7a03fe0c02a6aba3287a85abf7c62 $commit_sha12
7e3c53503a3db8dd996cb62e37c66e070b44b54d $commit_sha11
b8d03e173f67f6505a76f6e00cf93440200dd9be $commit_sha10
851e1638784a884c7dd26c5d41f3340f6387413a $commit_sha8
660311d7f78dc53db12ac373a43fca7465381a7e $commit_sha5
e2bfd06a37dd2031684a59a6e2b033e212239c78 $commit_sha4
5772f42408c0dd6f097a7ca2d24de0e78d1c46b1 $commit_sha3
283b48219aee9a4105f6cab337e789065c82c2b9 $commit_sha2
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
