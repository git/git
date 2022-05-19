#!/bin/sh

test_description='Test cummit notes organized in subtrees'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

number_of_cummits=100

start_note_cummit () {
	test_tick &&
	cat <<INPUT_END
cummit refs/notes/cummits
cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
data <<cummit
notes
cummit

from refs/notes/cummits^0
deleteall
INPUT_END

}

verify_notes () {
	but log | grep "^    " > output &&
	i=$number_of_cummits &&
	while [ $i -gt 0 ]; do
		echo "    cummit #$i" &&
		echo "    note for cummit #$i" &&
		i=$(($i-1)) || return 1
	done > expect &&
	test_cmp expect output
}

test_expect_success "setup: create $number_of_cummits cummits" '

	(
		nr=0 &&
		while [ $nr -lt $number_of_cummits ]; do
			nr=$(($nr+1)) &&
			test_tick &&
			cat <<INPUT_END || return 1
cummit refs/heads/main
cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
data <<cummit
cummit #$nr
cummit

M 644 inline file
data <<EOF
file in cummit #$nr
EOF

INPUT_END

		done &&
		test_tick &&
		cat <<INPUT_END
cummit refs/notes/cummits
cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
data <<cummit
no notes
cummit

deleteall

INPUT_END

	) |
	but fast-import --quiet &&
	but config core.notesRef refs/notes/cummits
'

test_sha1_based () {
	(
		start_note_cummit &&
		nr=$number_of_cummits &&
		but rev-list refs/heads/main >out &&
		while read sha1; do
			note_path=$(echo "$sha1" | sed "$1")
			cat <<INPUT_END &&
M 100644 inline $note_path
data <<EOF
note for cummit #$nr
EOF

INPUT_END

			nr=$(($nr-1))
		done <out
	) >gfi &&
	but fast-import --quiet <gfi
}

test_expect_success 'test notes in 2/38-fanout' 'test_sha1_based "s|^..|&/|"'
test_expect_success 'verify notes in 2/38-fanout' 'verify_notes'

test_expect_success 'test notes in 2/2/36-fanout' 'test_sha1_based "s|^\(..\)\(..\)|\1/\2/|"'
test_expect_success 'verify notes in 2/2/36-fanout' 'verify_notes'

test_expect_success 'test notes in 2/2/2/34-fanout' 'test_sha1_based "s|^\(..\)\(..\)\(..\)|\1/\2/\3/|"'
test_expect_success 'verify notes in 2/2/2/34-fanout' 'verify_notes'

test_same_notes () {
	(
		start_note_cummit &&
		nr=$number_of_cummits &&
		but rev-list refs/heads/main |
		while read sha1; do
			first_note_path=$(echo "$sha1" | sed "$1")
			second_note_path=$(echo "$sha1" | sed "$2")
			cat <<INPUT_END &&
M 100644 inline $second_note_path
data <<EOF
note for cummit #$nr
EOF

M 100644 inline $first_note_path
data <<EOF
note for cummit #$nr
EOF

INPUT_END

			nr=$(($nr-1))
		done
	) |
	but fast-import --quiet
}

test_expect_success 'test same notes in no fanout and 2/38-fanout' 'test_same_notes "s|^..|&/|" ""'
test_expect_success 'verify same notes in no fanout and 2/38-fanout' 'verify_notes'

test_expect_success 'test same notes in no fanout and 2/2/36-fanout' 'test_same_notes "s|^\(..\)\(..\)|\1/\2/|" ""'
test_expect_success 'verify same notes in no fanout and 2/2/36-fanout' 'verify_notes'

test_expect_success 'test same notes in 2/38-fanout and 2/2/36-fanout' 'test_same_notes "s|^\(..\)\(..\)|\1/\2/|" "s|^..|&/|"'
test_expect_success 'verify same notes in 2/38-fanout and 2/2/36-fanout' 'verify_notes'

test_expect_success 'test same notes in 2/2/2/34-fanout and 2/2/36-fanout' 'test_same_notes "s|^\(..\)\(..\)|\1/\2/|" "s|^\(..\)\(..\)\(..\)|\1/\2/\3/|"'
test_expect_success 'verify same notes in 2/2/2/34-fanout and 2/2/36-fanout' 'verify_notes'

test_concatenated_notes () {
	(
		start_note_cummit &&
		nr=$number_of_cummits &&
		but rev-list refs/heads/main |
		while read sha1; do
			first_note_path=$(echo "$sha1" | sed "$1")
			second_note_path=$(echo "$sha1" | sed "$2")
			cat <<INPUT_END &&
M 100644 inline $second_note_path
data <<EOF
second note for cummit #$nr
EOF

M 100644 inline $first_note_path
data <<EOF
first note for cummit #$nr
EOF

INPUT_END

			nr=$(($nr-1))
		done
	) |
	but fast-import --quiet
}

verify_concatenated_notes () {
	but log | grep "^    " > output &&
	i=$number_of_cummits &&
	while [ $i -gt 0 ]; do
		echo "    cummit #$i" &&
		echo "    first note for cummit #$i" &&
		echo "    " &&
		echo "    second note for cummit #$i" &&
		i=$(($i-1)) || return 1
	done > expect &&
	test_cmp expect output
}

test_expect_success 'test notes in no fanout concatenated with 2/38-fanout' 'test_concatenated_notes "s|^..|&/|" ""'
test_expect_success 'verify notes in no fanout concatenated with 2/38-fanout' 'verify_concatenated_notes'

test_expect_success 'test notes in no fanout concatenated with 2/2/36-fanout' 'test_concatenated_notes "s|^\(..\)\(..\)|\1/\2/|" ""'
test_expect_success 'verify notes in no fanout concatenated with 2/2/36-fanout' 'verify_concatenated_notes'

test_expect_success 'test notes in 2/38-fanout concatenated with 2/2/36-fanout' 'test_concatenated_notes "s|^\(..\)\(..\)|\1/\2/|" "s|^..|&/|"'
test_expect_success 'verify notes in 2/38-fanout concatenated with 2/2/36-fanout' 'verify_concatenated_notes'

test_expect_success 'test notes in 2/2/36-fanout concatenated with 2/2/2/34-fanout' 'test_concatenated_notes "s|^\(..\)\(..\)\(..\)|\1/\2/\3/|" "s|^\(..\)\(..\)|\1/\2/|"'
test_expect_success 'verify notes in 2/2/36-fanout concatenated with 2/2/2/34-fanout' 'verify_concatenated_notes'

test_done
