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
	git notes add -m "Notes on 4th commit" 4th
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
5e93d24084d32e1cb61f7070505b9d2530cca987 $commit_sha4
8366731eeee53787d2bdf8fc1eff7d94757e8da0 $commit_sha3
eede89064cd42441590d6afec6c37b321ada3389 $commit_sha2
daa55ffad6cb99bf64226532147ffcaf5ce8bdd1 $commit_sha1
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
cp expect_log_x expect_log_y

test_expect_success 'fail to merge empty notes ref into empty notes ref (z => y)' '
	test_must_fail git -c "core.notesRef=refs/notes/y" notes merge z
'

test_expect_success 'fail to merge into various non-notes refs' '
	test_must_fail git -c "core.notesRef=refs/notes" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/" notes merge x &&
	mkdir -p .git/refs/notes/dir &&
	test_must_fail git -c "core.notesRef=refs/notes/dir" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/dir/" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/heads/master" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/y:" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/y:foo" notes merge x &&
	test_must_fail git -c "core.notesRef=refs/notes/foo^{bar" notes merge x
'

test_expect_success 'fail to merge various non-note-trees' '
	git config core.notesRef refs/notes/y &&
	test_must_fail git notes merge refs/notes &&
	test_must_fail git notes merge refs/notes/ &&
	test_must_fail git notes merge refs/notes/dir &&
	test_must_fail git notes merge refs/notes/dir/ &&
	test_must_fail git notes merge refs/heads/master &&
	test_must_fail git notes merge x: &&
	test_must_fail git notes merge x:foo &&
	test_must_fail git notes merge foo^{bar
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
0f2efbd00262f2fd41dfae33df8765618eeacd99 $commit_sha5
dec2502dac3ea161543f71930044deff93fa945c $commit_sha4
4069cdb399fd45463ec6eef8e051a16a03592d91 $commit_sha3
daa55ffad6cb99bf64226532147ffcaf5ce8bdd1 $commit_sha1
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

test_done
