#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test commit notes index (expensive!)'

. ./test-lib.sh

test_set_prereq NOT_EXPENSIVE
test -n "$GIT_NOTES_TIMING_TESTS" && test_set_prereq EXPENSIVE
test -x /usr/bin/time && test_set_prereq USR_BIN_TIME

create_repo () {
	number_of_commits=$1
	nr=0
	test -d .git || {
	git init &&
	(
		while [ $nr -lt $number_of_commits ]; do
			nr=$(($nr+1))
			mark=$(($nr+$nr))
			notemark=$(($mark+1))
			test_tick &&
			cat <<INPUT_END &&
commit refs/heads/master
mark :$mark
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
commit #$nr
COMMIT

M 644 inline file
data <<EOF
file in commit #$nr
EOF

blob
mark :$notemark
data <<EOF
note for commit #$nr
EOF

INPUT_END

			echo "N :$notemark :$mark" >> note_commit
		done &&
		test_tick &&
		cat <<INPUT_END &&
commit refs/notes/commits
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
notes
COMMIT

INPUT_END

		cat note_commit
	) |
	git fast-import --quiet &&
	git config core.notesRef refs/notes/commits
	}
}

test_notes () {
	count=$1 &&
	git config core.notesRef refs/notes/commits &&
	git log | grep "^    " > output &&
	i=$count &&
	while [ $i -gt 0 ]; do
		echo "    commit #$i" &&
		echo "    note for commit #$i" &&
		i=$(($i-1));
	done > expect &&
	test_cmp expect output
}

cat > time_notes << \EOF
	mode=$1
	i=1
	while [ $i -lt $2 ]; do
		case $1 in
		no-notes)
			GIT_NOTES_REF=non-existing; export GIT_NOTES_REF
		;;
		notes)
			unset GIT_NOTES_REF
		;;
		esac
		git log >/dev/null
		i=$(($i+1))
	done
EOF

time_notes () {
	for mode in no-notes notes
	do
		echo $mode
		/usr/bin/time "$SHELL_PATH" ../time_notes $mode $1
	done
}

do_tests () {
	pr=$1
	count=$2

	test_expect_success $pr 'setup / mkdir' '
		mkdir $count &&
		cd $count
	'

	test_expect_success $pr "setup $count" "create_repo $count"

	test_expect_success $pr 'notes work' "test_notes $count"

	test_expect_success USR_BIN_TIME,$pr 'notes timing with /usr/bin/time' "time_notes 100"

	test_expect_success $pr 'teardown / cd ..' 'cd ..'
}

do_tests NOT_EXPENSIVE 10
for count in 100 1000 10000; do
	do_tests EXPENSIVE $count
done

test_done
