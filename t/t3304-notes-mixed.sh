#!/bin/sh

test_description='Test notes trees that also contain non-notes'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

number_of_cummits=100

start_note_cummit () {
	test_tick &&
	cat <<INPUT_END
cummit refs/notes/cummits
cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
data <<cummit
notes
cummit

from refs/notes/cummits^0
deleteall
INPUT_END

}

verify_notes () {
	git log | grep "^    " > output &&
	i=$number_of_cummits &&
	while [ $i -gt 0 ]; do
		echo "    cummit #$i" &&
		echo "    note for cummit #$i" &&
		i=$(($i-1));
	done > expect &&
	test_cmp expect output
}

test_expect_success "setup: create a couple of cummits" '

	test_tick &&
	cat <<INPUT_END >input &&
cummit refs/heads/main
cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
data <<cummit
cummit #1
cummit

M 644 inline file
data <<EOF
file in cummit #1
EOF

INPUT_END

	test_tick &&
	cat <<INPUT_END >>input &&
cummit refs/heads/main
cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
data <<cummit
cummit #2
cummit

M 644 inline file
data <<EOF
file in cummit #2
EOF

INPUT_END
	git fast-import --quiet <input
'

test_expect_success "create a notes tree with both notes and non-notes" '

	cummit1=$(git rev-parse refs/heads/main^) &&
	cummit2=$(git rev-parse refs/heads/main) &&
	test_tick &&
	cat <<INPUT_END >input &&
cummit refs/notes/cummits
cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
data <<cummit
notes cummit #1
cummit

N inline $cummit1
data <<EOF
note for cummit #1
EOF

N inline $cummit2
data <<EOF
note for cummit #2
EOF

INPUT_END
	test_tick &&
	cat <<INPUT_END >>input &&
cummit refs/notes/cummits
cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
data <<cummit
notes cummit #2
cummit

M 644 inline foobar/non-note.txt
data <<EOF
A non-note in a notes tree
EOF

N inline $cummit2
data <<EOF
edited note for cummit #2
EOF

INPUT_END
	test_tick &&
	cat <<INPUT_END >>input &&
cummit refs/notes/cummits
cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
data <<cummit
notes cummit #3
cummit

N inline $cummit1
data <<EOF
edited note for cummit #1
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
	git config core.notesRef refs/notes/cummits
'

cat >expect <<EXPECT_END
    cummit #2
    edited note for cummit #2
    cummit #1
    edited note for cummit #1
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

	git cat-file -p refs/notes/cummits:foobar/non-note.txt > actual_nn1 &&
	test_cmp expect_nn1 actual_nn1 &&
	git cat-file -p refs/notes/cummits:deadbeef > actual_nn2 &&
	test_cmp expect_nn2 actual_nn2 &&
	git cat-file -p refs/notes/cummits:de/adbeef > actual_nn3 &&
	test_cmp expect_nn3 actual_nn3 &&
	git cat-file -p refs/notes/cummits:dead/beef > actual_nn4 &&
	test_cmp expect_nn4 actual_nn4
'

test_expect_success "git-notes preserves non-notes" '

	test_tick &&
	git notes add -f -m "foo bar"
'

test_expect_success "verify contents of non-notes after git-notes" '

	git cat-file -p refs/notes/cummits:foobar/non-note.txt > actual_nn1 &&
	test_cmp expect_nn1 actual_nn1 &&
	git cat-file -p refs/notes/cummits:deadbeef > actual_nn2 &&
	test_cmp expect_nn2 actual_nn2 &&
	git cat-file -p refs/notes/cummits:de/adbeef > actual_nn3 &&
	test_cmp expect_nn3 actual_nn3 &&
	git cat-file -p refs/notes/cummits:dead/beef > actual_nn4 &&
	test_cmp expect_nn4 actual_nn4
'

test_done
