#!/bin/sh

test_description='Test commit notes organized in subtrees'

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

test_expect_success "setup: create $number_of_commits commits" '

	(
		nr=0 &&
		while [ $nr -lt $number_of_commits ]; do
			nr=$(($nr+1)) &&
			test_tick &&
			cat <<INPUT_END
commit refs/heads/master
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
commit #$nr
COMMIT

M 644 inline file
data <<EOF
file in commit #$nr
EOF

INPUT_END

		done &&
		test_tick &&
		cat <<INPUT_END
commit refs/notes/commits
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
no notes
COMMIT

deleteall

INPUT_END

	) |
	git fast-import --quiet &&
	git config core.notesRef refs/notes/commits
'

test_sha1_based () {
	(
		start_note_commit &&
		nr=$number_of_commits &&
		git rev-list refs/heads/master |
		while read sha1; do
			note_path=$(echo "$sha1" | sed "$1")
			cat <<INPUT_END &&
M 100644 inline $note_path
data <<EOF
note for commit #$nr
EOF

INPUT_END

			nr=$(($nr-1))
		done
	) |
	git fast-import --quiet
}

test_expect_success 'test notes in 2/38-fanout' 'test_sha1_based "s|^..|&/|"'
test_expect_success 'verify notes in 2/38-fanout' 'verify_notes'

test_expect_success 'test notes in 4/36-fanout' 'test_sha1_based "s|^....|&/|"'
test_expect_success 'verify notes in 4/36-fanout' 'verify_notes'

test_expect_success 'test notes in 2/2/36-fanout' 'test_sha1_based "s|^\(..\)\(..\)|\1/\2/|"'
test_expect_success 'verify notes in 2/2/36-fanout' 'verify_notes'

test_done
