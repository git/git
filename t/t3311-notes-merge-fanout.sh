#!/bin/sh
#
# Copyright (c) 2010 Johan Herland
#

test_description='Test notes merging at various fanout levels'

. ./test-lib.sh

verify_notes () {
	notes_ref="$1"
	commit="$2"
	if test -f "expect_notes_$notes_ref"
	then
		git -c core.notesRef="refs/notes/$notes_ref" notes |
			sort >"output_notes_$notes_ref" &&
		test_cmp "expect_notes_$notes_ref" "output_notes_$notes_ref" ||
			return 1
	fi &&
	git -c core.notesRef="refs/notes/$notes_ref" log --format="%H %s%n%N" \
		"$commit" >"output_log_$notes_ref" &&
	test_cmp "expect_log_$notes_ref" "output_log_$notes_ref"
}

verify_fanout () {
	notes_ref="$1"
	# Expect entire notes tree to have a fanout == 1
	git rev-parse --quiet --verify "refs/notes/$notes_ref" >/dev/null &&
	git ls-tree -r --name-only "refs/notes/$notes_ref" |
	while read path
	do
		echo "$path" | grep "^../[0-9a-f]*$" || {
			echo "Invalid path \"$path\"" &&
			return 1;
		}
	done
}

verify_no_fanout () {
	notes_ref="$1"
	# Expect entire notes tree to have a fanout == 0
	git rev-parse --quiet --verify "refs/notes/$notes_ref" >/dev/null &&
	git ls-tree -r --name-only "refs/notes/$notes_ref" |
	while read path
	do
		echo "$path" | grep -v "^../.*" || {
			echo "Invalid path \"$path\"" &&
			return 1;
		}
	done
}

# Set up a notes merge scenario with different kinds of conflicts
test_expect_success 'setup a few initial commits with notes (notes ref: x)' '
	git config core.notesRef refs/notes/x &&
	for i in 1 2 3 4 5
	do
		test_commit "commit$i" >/dev/null &&
		git notes add -m "notes for commit$i" || return 1
	done &&

	git log --format=oneline &&

	test_oid_cache <<-EOF
	hash05a sha1:aed91155c7a72c2188e781fdf40e0f3761b299db
	hash04a sha1:99fab268f9d7ee7b011e091a436c78def8eeee69
	hash03a sha1:953c20ae26c7aa0b428c20693fe38bc687f9d1a9
	hash02a sha1:6358796131b8916eaa2dde6902642942a1cb37e1
	hash01a sha1:b02d459c32f0e68f2fe0981033bb34f38776ba47
	hash03b sha1:9f506ee70e20379d7f78204c77b334f43d77410d
	hash02b sha1:23a47d6ea7d589895faf800752054818e1e7627b

	hash05a sha256:3aae5d26619d96dba93795f66325716e4cbc486884f95a6adee8fb0615a76d12
	hash04a sha256:07e43dd3d89fe634d3252e253b426aacc7285a995dcdbcf94ac284060a1122cf
	hash03a sha256:26fb52eaa7f4866bf735254587be7b31209ec10e525912ffd8e8ba549ba892ff
	hash02a sha256:b57ebdf23634e750dcbc4b9a37991d70f90830d568a0e4529ce9de0a3f8d605c
	hash01a sha256:377903b1572bd5117087a5518fcb1011b5053cccbc59e3c7c823a8615204173b
	hash03b sha256:04e7b392fda7c185bfa17c9179b56db732edc2dc2b3bf887308dcaabb717270d
	hash02b sha256:66099aaaec49a485ed990acadd9a9b81232ea592079964113d8f581ff69ef50b
	EOF
'

commit_sha1=$(git rev-parse commit1^{commit})
commit_sha2=$(git rev-parse commit2^{commit})
commit_sha3=$(git rev-parse commit3^{commit})
commit_sha4=$(git rev-parse commit4^{commit})
commit_sha5=$(git rev-parse commit5^{commit})

cat <<EOF | sort >expect_notes_x
$(test_oid hash05a) $commit_sha5
$(test_oid hash04a) $commit_sha4
$(test_oid hash03a) $commit_sha3
$(test_oid hash02a) $commit_sha2
$(test_oid hash01a) $commit_sha1
EOF

cat >expect_log_x <<EOF
$commit_sha5 commit5
notes for commit5

$commit_sha4 commit4
notes for commit4

$commit_sha3 commit3
notes for commit3

$commit_sha2 commit2
notes for commit2

$commit_sha1 commit1
notes for commit1

EOF

test_expect_success 'sanity check (x)' '
	verify_notes x commit5 &&
	verify_no_fanout x
'

num=300

cp expect_log_x expect_log_y

test_expect_success 'Add a few hundred commits w/notes to trigger fanout (x -> y)' '
	git update-ref refs/notes/y refs/notes/x &&
	git config core.notesRef refs/notes/y &&
	test_commit_bulk --start=6 --id=commit $((num - 5)) &&
	i=0 &&
	while test $i -lt $((num - 5))
	do
		git notes add -m "notes for commit$i" HEAD~$i || return 1
		i=$((i + 1))
	done &&
	test "$(git rev-parse refs/notes/y)" != "$(git rev-parse refs/notes/x)" &&
	# Expected number of commits and notes
	test $(git rev-list HEAD | wc -l) = $num &&
	test $(git notes list | wc -l) = $num &&
	# 5 first notes unchanged
	verify_notes y commit5
'

test_expect_success 'notes tree has fanout (y)' 'verify_fanout y'

test_expect_success 'No-op merge (already included) (x => y)' '
	git update-ref refs/notes/m refs/notes/y &&
	git config core.notesRef refs/notes/m &&
	git notes merge x &&
	test "$(git rev-parse refs/notes/m)" = "$(git rev-parse refs/notes/y)"
'

test_expect_success 'Fast-forward merge (y => x)' '
	git update-ref refs/notes/m refs/notes/x &&
	git notes merge y &&
	test "$(git rev-parse refs/notes/m)" = "$(git rev-parse refs/notes/y)"
'

cat <<EOF | sort >expect_notes_z
$(test_oid hash03b) $commit_sha3
$(test_oid hash02b) $commit_sha2
$(test_oid hash01a) $commit_sha1
EOF

cat >expect_log_z <<EOF
$commit_sha5 commit5

$commit_sha4 commit4

$commit_sha3 commit3
notes for commit3

appended notes for commit3

$commit_sha2 commit2
new notes for commit2

$commit_sha1 commit1
notes for commit1

EOF

test_expect_success 'change some of the initial 5 notes (x -> z)' '
	git update-ref refs/notes/z refs/notes/x &&
	git config core.notesRef refs/notes/z &&
	git notes add -f -m "new notes for commit2" commit2 &&
	git notes append -m "appended notes for commit3" commit3 &&
	git notes remove commit4 &&
	git notes remove commit5 &&
	verify_notes z commit5
'

test_expect_success 'notes tree has no fanout (z)' 'verify_no_fanout z'

cp expect_log_z expect_log_m

test_expect_success 'successful merge without conflicts (y => z)' '
	git update-ref refs/notes/m refs/notes/z &&
	git config core.notesRef refs/notes/m &&
	git notes merge y &&
	verify_notes m commit5 &&
	# x/y/z unchanged
	verify_notes x commit5 &&
	verify_notes y commit5 &&
	verify_notes z commit5
'

test_expect_success 'notes tree still has fanout after merge (m)' 'verify_fanout m'

cat >expect_log_w <<EOF
$commit_sha5 commit5

$commit_sha4 commit4
other notes for commit4

$commit_sha3 commit3
other notes for commit3

$commit_sha2 commit2
notes for commit2

$commit_sha1 commit1
other notes for commit1

EOF

test_expect_success 'introduce conflicting changes (y -> w)' '
	git update-ref refs/notes/w refs/notes/y &&
	git config core.notesRef refs/notes/w &&
	git notes add -f -m "other notes for commit1" commit1 &&
	git notes add -f -m "other notes for commit3" commit3 &&
	git notes add -f -m "other notes for commit4" commit4 &&
	git notes remove commit5 &&
	verify_notes w commit5
'

cat >expect_log_m <<EOF
$commit_sha5 commit5

$commit_sha4 commit4
other notes for commit4

$commit_sha3 commit3
other notes for commit3

$commit_sha2 commit2
new notes for commit2

$commit_sha1 commit1
other notes for commit1

EOF

test_expect_success 'successful merge using "ours" strategy (z => w)' '
	git update-ref refs/notes/m refs/notes/w &&
	git config core.notesRef refs/notes/m &&
	git notes merge -s ours z &&
	verify_notes m commit5 &&
	# w/x/y/z unchanged
	verify_notes w commit5 &&
	verify_notes x commit5 &&
	verify_notes y commit5 &&
	verify_notes z commit5
'

test_expect_success 'notes tree still has fanout after merge (m)' 'verify_fanout m'

cat >expect_log_m <<EOF
$commit_sha5 commit5

$commit_sha4 commit4

$commit_sha3 commit3
notes for commit3

appended notes for commit3

$commit_sha2 commit2
new notes for commit2

$commit_sha1 commit1
other notes for commit1

EOF

test_expect_success 'successful merge using "theirs" strategy (z => w)' '
	git update-ref refs/notes/m refs/notes/w &&
	git notes merge -s theirs z &&
	verify_notes m commit5 &&
	# w/x/y/z unchanged
	verify_notes w commit5 &&
	verify_notes x commit5 &&
	verify_notes y commit5 &&
	verify_notes z commit5
'

test_expect_success 'notes tree still has fanout after merge (m)' 'verify_fanout m'

cat >expect_log_m <<EOF
$commit_sha5 commit5

$commit_sha4 commit4
other notes for commit4

$commit_sha3 commit3
other notes for commit3

notes for commit3

appended notes for commit3

$commit_sha2 commit2
new notes for commit2

$commit_sha1 commit1
other notes for commit1

EOF

test_expect_success 'successful merge using "union" strategy (z => w)' '
	git update-ref refs/notes/m refs/notes/w &&
	git notes merge -s union z &&
	verify_notes m commit5 &&
	# w/x/y/z unchanged
	verify_notes w commit5 &&
	verify_notes x commit5 &&
	verify_notes y commit5 &&
	verify_notes z commit5
'

test_expect_success 'notes tree still has fanout after merge (m)' 'verify_fanout m'

cat >expect_log_m <<EOF
$commit_sha5 commit5

$commit_sha4 commit4
other notes for commit4

$commit_sha3 commit3
appended notes for commit3
notes for commit3
other notes for commit3

$commit_sha2 commit2
new notes for commit2

$commit_sha1 commit1
other notes for commit1

EOF

test_expect_success 'successful merge using "cat_sort_uniq" strategy (z => w)' '
	git update-ref refs/notes/m refs/notes/w &&
	git notes merge -s cat_sort_uniq z &&
	verify_notes m commit5 &&
	# w/x/y/z unchanged
	verify_notes w commit5 &&
	verify_notes x commit5 &&
	verify_notes y commit5 &&
	verify_notes z commit5
'

test_expect_success 'notes tree still has fanout after merge (m)' 'verify_fanout m'

# We're merging z into w. Here are the conflicts we expect:
#
# commit | x -> w    | x -> z    | conflict?
# -------|-----------|-----------|----------
# 1      | changed   | unchanged | no, use w
# 2      | unchanged | changed   | no, use z
# 3      | changed   | changed   | yes (w, then z in conflict markers)
# 4      | changed   | deleted   | yes (w)
# 5      | deleted   | deleted   | no, deleted

test_expect_success 'fails to merge using "manual" strategy (z => w)' '
	git update-ref refs/notes/m refs/notes/w &&
	test_must_fail git notes merge z
'

test_expect_success 'notes tree still has fanout after merge (m)' 'verify_fanout m'

cat <<EOF | sort >expect_conflicts
$commit_sha3
$commit_sha4
EOF

cat >expect_conflict_$commit_sha3 <<EOF
<<<<<<< refs/notes/m
other notes for commit3
=======
notes for commit3

appended notes for commit3
>>>>>>> refs/notes/z
EOF

cat >expect_conflict_$commit_sha4 <<EOF
other notes for commit4
EOF

test_expect_success 'verify conflict entries (with no fanout)' '
	ls .git/NOTES_MERGE_WORKTREE >output_conflicts &&
	test_cmp expect_conflicts output_conflicts &&
	( for f in $(cat expect_conflicts); do
		test_cmp "expect_conflict_$f" ".git/NOTES_MERGE_WORKTREE/$f" ||
		exit 1
	done ) &&
	# Verify that current notes tree (pre-merge) has not changed (m == w)
	test "$(git rev-parse refs/notes/m)" = "$(git rev-parse refs/notes/w)"
'

cat >expect_log_m <<EOF
$commit_sha5 commit5

$commit_sha4 commit4
other notes for commit4

$commit_sha3 commit3
other notes for commit3

appended notes for commit3

$commit_sha2 commit2
new notes for commit2

$commit_sha1 commit1
other notes for commit1

EOF

test_expect_success 'resolve and finalize merge (z => w)' '
	cat >.git/NOTES_MERGE_WORKTREE/$commit_sha3 <<EOF &&
other notes for commit3

appended notes for commit3
EOF
	git notes merge --commit &&
	verify_notes m commit5 &&
	# w/x/y/z unchanged
	verify_notes w commit5 &&
	verify_notes x commit5 &&
	verify_notes y commit5 &&
	verify_notes z commit5
'

test_expect_success 'notes tree still has fanout after merge (m)' 'verify_fanout m'

test_done
