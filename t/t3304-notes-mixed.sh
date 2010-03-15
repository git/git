#!/bin/sh

test_description='Test notes trees that also contain non-notes'

. ./test-lib.sh

number_of_commits=100

start_note_commit () {
	test_tick &&
	cat <<INPUT_END
commit refs/notes/commits
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
notes
COMMIT

from refs/notes/commits^0
deleteall
INPUT_END

}

verify_notes () {
	git log | grep "^    " > output &&
	i=$number_of_commits &&
	while [ $i -gt 0 ]; do
		echo "    commit #$i" &&
		echo "    note for commit #$i" &&
		i=$(($i-1));
	done > expect &&
	test_cmp expect output
}

test_expect_success "setup: create a couple of commits" '

	test_tick &&
	cat <<INPUT_END >input &&
commit refs/heads/master
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
commit #1
COMMIT

M 644 inline file
data <<EOF
file in commit #1
EOF

INPUT_END

	test_tick &&
	cat <<INPUT_END >>input &&
commit refs/heads/master
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
commit #2
COMMIT

M 644 inline file
data <<EOF
file in commit #2
EOF

INPUT_END
	git fast-import --quiet <input
'

test_expect_success "create a notes tree with both notes and non-notes" '

	commit1=$(git rev-parse refs/heads/master^) &&
	commit2=$(git rev-parse refs/heads/master) &&
	test_tick &&
	cat <<INPUT_END >input &&
commit refs/notes/commits
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
notes commit #1
COMMIT

N inline $commit1
data <<EOF
note for commit #1
EOF

N inline $commit2
data <<EOF
note for commit #2
EOF

INPUT_END
	test_tick &&
	cat <<INPUT_END >>input &&
commit refs/notes/commits
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
notes commit #2
COMMIT

M 644 inline foobar/non-note.txt
data <<EOF
A non-note in a notes tree
EOF

N inline $commit2
data <<EOF
edited note for commit #2
EOF

INPUT_END
	test_tick &&
	cat <<INPUT_END >>input &&
commit refs/notes/commits
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
notes commit #3
COMMIT

N inline $commit1
data <<EOF
edited note for commit #1
EOF

M 644 inline deadbeef
data <<EOF
non-note with SHA1-like name
EOF

M 644 inline de/adbeef
data <<EOF
another non-note with SHA1-like name
EOF

M 644 inline de/adbeefdeadbeefdeadbeefdeadbeefdeadbeef
data <<EOF
This is actually a valid note, albeit to a non-existing object.
It is needed in order to trigger the "mishandling" of the dead/beef non-note.
EOF

M 644 inline dead/beef
data <<EOF
yet another non-note with SHA1-like name
EOF

INPUT_END
	git fast-import --quiet <input &&
	git config core.notesRef refs/notes/commits
'

cat >expect <<EXPECT_END
    commit #2
    edited note for commit #2
    commit #1
    edited note for commit #1
EXPECT_END

test_expect_success "verify contents of notes" '

	git log | grep "^    " > actual &&
	test_cmp expect actual
'

cat >expect_nn1 <<EXPECT_END
A non-note in a notes tree
EXPECT_END
cat >expect_nn2 <<EXPECT_END
non-note with SHA1-like name
EXPECT_END
cat >expect_nn3 <<EXPECT_END
another non-note with SHA1-like name
EXPECT_END
cat >expect_nn4 <<EXPECT_END
yet another non-note with SHA1-like name
EXPECT_END

test_expect_success "verify contents of non-notes" '

	git cat-file -p refs/notes/commits:foobar/non-note.txt > actual_nn1 &&
	test_cmp expect_nn1 actual_nn1 &&
	git cat-file -p refs/notes/commits:deadbeef > actual_nn2 &&
	test_cmp expect_nn2 actual_nn2 &&
	git cat-file -p refs/notes/commits:de/adbeef > actual_nn3 &&
	test_cmp expect_nn3 actual_nn3 &&
	git cat-file -p refs/notes/commits:dead/beef > actual_nn4 &&
	test_cmp expect_nn4 actual_nn4
'

test_expect_success "git-notes preserves non-notes" '

	test_tick &&
	git notes add -f -m "foo bar"
'

test_expect_success "verify contents of non-notes after git-notes" '

	git cat-file -p refs/notes/commits:foobar/non-note.txt > actual_nn1 &&
	test_cmp expect_nn1 actual_nn1 &&
	git cat-file -p refs/notes/commits:deadbeef > actual_nn2 &&
	test_cmp expect_nn2 actual_nn2 &&
	git cat-file -p refs/notes/commits:de/adbeef > actual_nn3 &&
	test_cmp expect_nn3 actual_nn3 &&
	git cat-file -p refs/notes/commits:dead/beef > actual_nn4 &&
	test_cmp expect_nn4 actual_nn4
'

test_done
