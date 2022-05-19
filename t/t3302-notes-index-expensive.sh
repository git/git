#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test cummit notes index (expensive!)'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

create_repo () {
	number_of_cummits=$1
	nr=0
	test -d .git || {
	git init &&
	(
		while test $nr -lt $number_of_cummits
		do
			nr=$(($nr+1))
			mark=$(($nr+$nr))
			notemark=$(($mark+1))
			test_tick &&
			cat <<-INPUT_END &&
			cummit refs/heads/main
			mark :$mark
			cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
			data <<cummit
			cummit #$nr
			cummit

			M 644 inline file
			data <<EOF
			file in cummit #$nr
			EOF

			blob
			mark :$notemark
			data <<EOF
			note for cummit #$nr
			EOF

			INPUT_END
			echo "N :$notemark :$mark" >>note_cummit
		done &&
		test_tick &&
		cat <<-INPUT_END &&
		cummit refs/notes/cummits
		cummitter $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
		data <<cummit
		notes
		cummit

		INPUT_END

		cat note_cummit
	) |
	git fast-import --quiet &&
	git config core.notesRef refs/notes/cummits
	}
}

test_notes () {
	count=$1 &&
	git config core.notesRef refs/notes/cummits &&
	git log >tmp &&
	grep "^    " tmp >output &&
	i=$count &&
	while test $i -gt 0
	do
		echo "    cummit #$i" &&
		echo "    note for cummit #$i" &&
		i=$(($i-1))
	done >expect &&
	test_cmp expect output
}

write_script time_notes <<\EOF
	mode=$1
	i=1
	while test $i -lt $2
	do
		case $1 in
		no-notes)
			GIT_NOTES_REF=non-existing
			export GIT_NOTES_REF
			;;
		notes)
			unset GIT_NOTES_REF
			;;
		esac
		git log || exit $?
		i=$(($i+1))
	done >/dev/null
EOF

time_notes () {
	for mode in no-notes notes
	do
		echo $mode
		/usr/bin/time ../time_notes $mode $1
	done
}

do_tests () {
	count=$1 pr=${2-}

	test_expect_success $pr "setup $count" '
		mkdir "$count" &&
		(
			cd "$count" &&
			create_repo "$count"
		)
	'

	test_expect_success $pr 'notes work' '
		(
			cd "$count" &&
			test_notes "$count"
		)
	'

	test_expect_success "USR_BIN_TIME${pr:+,$pr}" 'notes timing with /usr/bin/time' '
		(
			cd "$count" &&
			time_notes 100
		)
	'
}

do_tests 10
for count in 100 1000 10000
do
	do_tests "$count" EXPENSIVE
done

test_done
