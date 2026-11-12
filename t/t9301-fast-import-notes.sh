#!/bin/sh
#
# Copyright (c) 2009 Johan Herland
#

test_description='test git fast-import of notes objects'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


test_tick
cat >input <<INPUT_END
commit refs/heads/main
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
first commit
COMMIT

M 644 inline foo
data <<EOF
file foo in first commit
EOF

M 755 inline bar
data <<EOF
file bar in first commit
EOF

M 644 inline baz/xyzzy
data <<EOF
file baz/xyzzy in first commit
EOF

commit refs/heads/main
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
second commit
COMMIT

M 644 inline foo
data <<EOF
file foo in second commit
EOF

M 755 inline baz/xyzzy
data <<EOF
file baz/xyzzy in second commit
EOF

commit refs/heads/main
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
third commit
COMMIT

M 644 inline foo
data <<EOF
file foo in third commit
EOF

commit refs/heads/main
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
fourth commit
COMMIT

M 755 inline bar
data <<EOF
file bar in fourth commit
EOF

INPUT_END

test_expect_success 'set up main branch' '

	git fast-import <input &&
	git log --raw main
'

commit4=$(git rev-parse refs/heads/main)
commit3=$(git rev-parse "$commit4^")
commit2=$(git rev-parse "$commit4~2")
commit1=$(git rev-parse "$commit4~3")

test_tick
cat >input <<INPUT_END
commit refs/notes/test
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
first notes commit
COMMIT

M 644 inline $commit1
data <<EOF
first note for first commit
EOF

M 755 inline $commit2
data <<EOF
first note for second commit
EOF

INPUT_END

cat >expect <<EXPECT_END
    fourth commit
    third commit
    second commit
    first note for second commit
    first commit
    first note for first commit
EXPECT_END

test_expect_success 'add notes with simple M command' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
feature notes
commit refs/notes/test
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
second notes commit
COMMIT

from refs/notes/test^0
N inline $commit3
data <<EOF
first note for third commit
EOF

N inline $commit4
data <<EOF
first note for fourth commit
EOF

INPUT_END

cat >expect <<EXPECT_END
    fourth commit
    first note for fourth commit
    third commit
    first note for third commit
    second commit
    first note for second commit
    first commit
    first note for first commit
EXPECT_END

test_expect_success 'add notes with simple N command' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
commit refs/notes/test
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
third notes commit
COMMIT

from refs/notes/test^0
N inline $commit1
data <<EOF
second note for first commit
EOF

N inline $commit2
data <<EOF
second note for second commit
EOF

N inline $commit3
data <<EOF
second note for third commit
EOF

N inline $commit4
data <<EOF
second note for fourth commit
EOF

INPUT_END

cat >expect <<EXPECT_END
    fourth commit
    second note for fourth commit
    third commit
    second note for third commit
    second commit
    second note for second commit
    first commit
    second note for first commit
EXPECT_END

test_expect_success 'update existing notes with N command' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
commit refs/notes/test
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
fourth notes commit
COMMIT

from refs/notes/test^0
M 644 inline $(echo "$commit3" | sed "s|^..|&/|")
data <<EOF
prefix of note for third commit
EOF

M 644 inline $(echo "$commit4" | sed "s|^..|&/|")
data <<EOF
prefix of note for fourth commit
EOF

M 644 inline $(echo "$commit4" | sed "s|^\(..\)\(..\)|\1/\2/|")
data <<EOF
pre-prefix of note for fourth commit
EOF

N inline $commit1
data <<EOF
third note for first commit
EOF

N inline $commit2
data <<EOF
third note for second commit
EOF

N inline $commit3
data <<EOF
third note for third commit
EOF

N inline $commit4
data <<EOF
third note for fourth commit
EOF


INPUT_END

whitespace="    "

cat >expect <<EXPECT_END
    fourth commit
    pre-prefix of note for fourth commit
$whitespace
    prefix of note for fourth commit
$whitespace
    third note for fourth commit
    third commit
    prefix of note for third commit
$whitespace
    third note for third commit
    second commit
    third note for second commit
    first commit
    third note for first commit
EXPECT_END

test_expect_success 'add concatenation notes with M command' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
commit refs/notes/test
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
fifth notes commit
COMMIT

from refs/notes/test^0
deleteall

INPUT_END

cat >expect <<EXPECT_END
    fourth commit
    third commit
    second commit
    first commit
EXPECT_END

test_expect_success 'verify that deleteall also removes notes' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
commit refs/notes/test
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
sixth notes commit
COMMIT

from refs/notes/test^0
M 644 inline $commit1
data <<EOF
third note for first commit
EOF

M 644 inline $commit3
data <<EOF
third note for third commit
EOF

N inline $commit1
data <<EOF
fourth note for first commit
EOF

N inline $commit3
data <<EOF
fourth note for third commit
EOF

INPUT_END

cat >expect <<EXPECT_END
    fourth commit
    third commit
    fourth note for third commit
    second commit
    first commit
    fourth note for first commit
EXPECT_END

test_expect_success 'verify that later N commands override earlier M commands' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

# Write fast-import commands to create the given number of commits
fast_import_commits () {
	my_ref=$1
	my_num_commits=$2
	my_append_to_file=$3
	my_i=0
	while test $my_i -lt $my_num_commits
	do
		my_i=$(($my_i + 1))
		test_tick
		cat >>"$my_append_to_file" <<INPUT_END
commit $my_ref
mark :$my_i
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
commit #$my_i
COMMIT

M 644 inline file
data <<EOF
file contents in commit #$my_i
EOF

INPUT_END
	done
}

# Write fast-import commands to create the given number of notes annotating
# the commits created by fast_import_commits()
fast_import_notes () {
	my_notes_ref=$1
	my_num_commits=$2
	my_append_to_file=$3
	my_note_append=$4
	test_tick
	cat >>"$my_append_to_file" <<INPUT_END
commit $my_notes_ref
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
committing $my_num_commits notes
COMMIT

INPUT_END

	my_i=0
	while test $my_i -lt $my_num_commits
	do
		my_i=$(($my_i + 1))
		cat >>"$my_append_to_file" <<INPUT_END
N inline :$my_i
data <<EOF
note for commit #$my_i$my_note_append
EOF

INPUT_END
	done
}


rm input expect
num_commits=400
# Create lots of commits
fast_import_commits "refs/heads/many_commits" $num_commits input
# Create one note per above commit
fast_import_notes "refs/notes/many_notes" $num_commits input
# Add a couple of non-notes as well
test_tick
cat >>input <<INPUT_END
commit refs/notes/many_notes
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
committing some non-notes to the notes tree
COMMIT

M 755 inline foobar/non-note.txt
data <<EOF
This is not a note, but rather a regular file residing in a notes tree
EOF

M 644 inline deadbeef
data <<EOF
Non-note file
EOF

M 644 inline de/adbeef
data <<EOF
Another non-note file
EOF

INPUT_END
# Finally create the expected output from all these notes and commits
i=$num_commits
while test $i -gt 0
do
	cat >>expect <<EXPECT_END
    commit #$i
    note for commit #$i
EXPECT_END
	i=$(($i - 1))
done

test_expect_success 'add lots of commits and notes' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/many_notes git log refs/heads/many_commits |
	    grep "^    " > actual &&
	test_cmp expect actual

'

test_expect_success 'verify that lots of notes trigger a fanout scheme' '
	hexsz=$(test_oid hexsz) &&

	# None of the entries in the top-level notes tree should be a full SHA1
	git ls-tree --name-only refs/notes/many_notes |
	while read path
	do
		if test $(expr length "$path") -ge $hexsz
		then
			return 1
		fi
	done

'

# Create another notes tree from the one above
SP=" "
cat >>input <<INPUT_END
commit refs/heads/other_commits
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
commit #$(($num_commit + 1))
COMMIT

from refs/heads/many_commits
M 644 inline file
data <<EOF
file contents in commit #$(($num_commit + 1))
EOF

commit refs/notes/other_notes
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
committing one more note on a tree imported from a previous notes tree
COMMIT

M 040000 $(git log --no-walk --format=%T refs/notes/many_notes)$SP
N inline :$(($num_commit + 1))
data <<EOF
note for commit #$(($num_commit + 1))
EOF
INPUT_END

test_expect_success 'verify that importing a notes tree respects the fanout scheme' '
	git fast-import <input &&

	# None of the entries in the top-level notes tree should be a full SHA1
	git ls-tree --name-only refs/notes/other_notes |
	while read path
	do
		if test $(expr length "$path") -ge $hexsz
		then
			return 1
		fi
	done
'

cat >>expect_non-note1 << EOF
This is not a note, but rather a regular file residing in a notes tree
EOF

cat >>expect_non-note2 << EOF
Non-note file
EOF

cat >>expect_non-note3 << EOF
Another non-note file
EOF

test_expect_success 'verify that non-notes are untouched by a fanout change' '

	git cat-file -p refs/notes/many_notes:foobar/non-note.txt > actual &&
	test_cmp expect_non-note1 actual &&
	git cat-file -p refs/notes/many_notes:deadbeef > actual &&
	test_cmp expect_non-note2 actual &&
	git cat-file -p refs/notes/many_notes:de/adbeef > actual &&
	test_cmp expect_non-note3 actual

'

# Change the notes for the three top commits
test_tick
cat >input <<INPUT_END
commit refs/notes/many_notes
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
changing notes for the top three commits
COMMIT
from refs/notes/many_notes^0
INPUT_END

rm expect
i=$num_commits
j=0
while test $j -lt 3
do
	cat >>input <<INPUT_END
N inline refs/heads/many_commits~$j
data <<EOF
changed note for commit #$i
EOF
INPUT_END
	cat >>expect <<EXPECT_END
    commit #$i
    changed note for commit #$i
EXPECT_END
	i=$(($i - 1))
	j=$(($j + 1))
done

test_expect_success 'change a few existing notes' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/many_notes git log -n3 refs/heads/many_commits |
	    grep "^    " > actual &&
	test_cmp expect actual

'

test_expect_success 'verify that changing notes respect existing fanout' '

	# None of the entries in the top-level notes tree should be a full SHA1
	git ls-tree --name-only refs/notes/many_notes |
	while read path
	do
		if test $(expr length "$path") -ge $hexsz
		then
			return 1
		fi
	done

'

remaining_notes=10
test_tick
cat >input <<INPUT_END
commit refs/notes/many_notes
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
removing all notes but $remaining_notes
COMMIT
from refs/notes/many_notes^0
INPUT_END

i=$(($num_commits - $remaining_notes))
for sha1 in $(git rev-list -n $i refs/heads/many_commits)
do
	cat >>input <<INPUT_END
N $ZERO_OID $sha1
INPUT_END
done

i=$num_commits
rm expect
while test $i -gt 0
do
	cat >>expect <<EXPECT_END
    commit #$i
EXPECT_END
	if test $i -le $remaining_notes
	then
		cat >>expect <<EXPECT_END
    note for commit #$i
EXPECT_END
	fi
	i=$(($i - 1))
done

test_expect_success 'remove lots of notes' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/many_notes git log refs/heads/many_commits |
	    grep "^    " > actual &&
	test_cmp expect actual

'

test_expect_success 'verify that removing notes trigger fanout consolidation' '
	# All entries in the top-level notes tree should be a full SHA1
	git ls-tree --name-only -r refs/notes/many_notes |
	while read path
	do
		# Explicitly ignore the non-note paths
		test "$path" = "foobar/non-note.txt" && continue
		test "$path" = "deadbeef" && continue
		test "$path" = "de/adbeef" && continue

		if test $(expr length "$path") -ne $hexsz
		then
			return 1
		fi
	done

'

test_expect_success 'verify that non-notes are untouched by a fanout change' '

	git cat-file -p refs/notes/many_notes:foobar/non-note.txt > actual &&
	test_cmp expect_non-note1 actual &&
	git cat-file -p refs/notes/many_notes:deadbeef > actual &&
	test_cmp expect_non-note2 actual &&
	git cat-file -p refs/notes/many_notes:de/adbeef > actual &&
	test_cmp expect_non-note3 actual

'


rm input expect
num_notes_refs=10
num_commits=16
some_commits=8
# Create commits
fast_import_commits "refs/heads/more_commits" $num_commits input
# Create one note per above commit per notes ref
i=0
while test $i -lt $num_notes_refs
do
	i=$(($i + 1))
	fast_import_notes "refs/notes/more_notes_$i" $num_commits input
done
# Trigger branch reloading in git-fast-import by repeating the note creation
i=0
while test $i -lt $num_notes_refs
do
	i=$(($i + 1))
	fast_import_notes "refs/notes/more_notes_$i" $some_commits input " (2)"
done
# Finally create the expected output from the notes in refs/notes/more_notes_1
i=$num_commits
while test $i -gt 0
do
	note_data="note for commit #$i"
	if test $i -le $some_commits
	then
		note_data="$note_data (2)"
	fi
	cat >>expect <<EXPECT_END
    commit #$i
    $note_data
EXPECT_END
	i=$(($i - 1))
done

test_expect_success "add notes to $num_commits commits in each of $num_notes_refs refs" '

	git fast-import --active-branches=5 <input &&
	GIT_NOTES_REF=refs/notes/more_notes_1 git log refs/heads/more_commits |
	    grep "^    " > actual &&
	test_cmp expect actual

'

setup_notes_repo () {
	test_when_finished "rm -rf repo" &&
	git init repo &&
	file2_data="file2
second line of EOF" &&
	file4_data=abcd &&
	file4_len=4 &&
	file5_data="an inline file.
  we should see it later." &&
	file6_data="#!/bin/sh
echo \"\$@\"" &&
	note1_data="The first note for the first commit" &&
	note2_data="The first note for the second commit" &&
	note3_data="The first note for the third commit" &&
	note1b_data="The second note for the first commit" &&
	note1c_data="The third note for the first commit" &&
	note2b_data="The second note for the second commit" &&

	test_tick &&
	cat >repo/input <<-INPUT_END &&
	blob
	mark :2
	data <<EOF
	$file2_data
	EOF

	commit refs/heads/notes-test
	mark :3
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	first (:3)
	COMMIT

	M 644 :2 file2

	blob
	mark :4
	data $file4_len
	$file4_data
	commit refs/heads/notes-test
	mark :5
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	second (:5)
	COMMIT

	M 644 :4 file4

	commit refs/heads/notes-test
	mark :6
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	third (:6)
	COMMIT

	M 644 inline file5
	data <<EOF
	$file5_data
	EOF

	M 755 inline file6
	data <<EOF
	$file6_data
	EOF

	blob
	mark :7
	data <<EOF
	$note1_data
	EOF

	blob
	mark :8
	data <<EOF
	$note2_data
	EOF

	commit refs/notes/foobar
	mark :9
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	notes (:9)
	COMMIT

	N :7 :3
	N :8 :5
	N inline :6
	data <<EOF
	$note3_data
	EOF

	commit refs/notes/foobar
	mark :10
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	notes (:10)
	COMMIT

	N inline :3
	data <<EOF
	$note1b_data
	EOF

	commit refs/notes/foobar2
	mark :11
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	notes (:11)
	COMMIT

	N inline :3
	data <<EOF
	$note1c_data
	EOF

	commit refs/notes/foobar
	mark :12
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	notes (:12)
	COMMIT

	deleteall
	N inline :5
	data <<EOF
	$note2b_data
	EOF

	INPUT_END

	git -C repo fast-import <repo/input &&
	commit1=$(git -C repo rev-parse notes-test~2) &&
	commit2=$(git -C repo rev-parse notes-test^) &&
	commit3=$(git -C repo rev-parse notes-test)
}

test_expect_success 'verify first commit' '
	setup_notes_repo &&
	(
		cd repo &&
		cat >expect <<-EOF &&
		author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

		first (:3)
		EOF
		git cat-file commit notes-test~2 | sed 1d >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify second commit' '
	setup_notes_repo &&
	(
		cd repo &&
		cat >expect <<-EOF &&
		parent $commit1
		author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

		second (:5)
		EOF
		git cat-file commit notes-test^ | sed 1d >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify third commit' '
	setup_notes_repo &&
	(
		cd repo &&
		cat >expect <<-EOF &&
		parent $commit2
		author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

		third (:6)
		EOF
		git cat-file commit notes-test | sed 1d >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify first notes commit' '
	setup_notes_repo &&
	(
		cd repo &&
		cat >expect <<-EOF &&
		author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

		notes (:9)
		EOF
		git cat-file commit refs/notes/foobar~2 | sed 1d >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify first notes tree' '
	setup_notes_repo &&
	(
		cd repo &&
		sort >expect <<-EOF &&
		100644 blob $commit1
		100644 blob $commit2
		100644 blob $commit3
		EOF
		git cat-file -p refs/notes/foobar~2^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify first note for first commit' '
	setup_notes_repo &&
	(
		cd repo &&
		echo "$note1_data" >expect &&
		git cat-file blob refs/notes/foobar~2:$commit1 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify first note for second commit' '
	setup_notes_repo &&
	(
		cd repo &&
		echo "$note2_data" >expect &&
		git cat-file blob refs/notes/foobar~2:$commit2 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify first note for third commit' '
	setup_notes_repo &&
	(
		cd repo &&
		echo "$note3_data" >expect &&
		git cat-file blob refs/notes/foobar~2:$commit3 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify second notes commit' '
	setup_notes_repo &&
	(
		cd repo &&
		cat >expect <<-EOF &&
		parent $(git rev-parse --verify refs/notes/foobar~2)
		author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

		notes (:10)
		EOF
		git cat-file commit refs/notes/foobar^ | sed 1d >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify second notes tree' '
	setup_notes_repo &&
	(
		cd repo &&
		sort >expect <<-EOF &&
		100644 blob $commit1
		100644 blob $commit2
		100644 blob $commit3
		EOF
		git cat-file -p refs/notes/foobar^^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify second note for first commit' '
	setup_notes_repo &&
	(
		cd repo &&
		echo "$note1b_data" >expect &&
		git cat-file blob refs/notes/foobar^:$commit1 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify first note for second commit' '
	setup_notes_repo &&
	(
		cd repo &&
		echo "$note2_data" >expect &&
		git cat-file blob refs/notes/foobar^:$commit2 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify first note for third commit' '
	setup_notes_repo &&
	(
		cd repo &&
		echo "$note3_data" >expect &&
		git cat-file blob refs/notes/foobar^:$commit3 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify third notes commit' '
	setup_notes_repo &&
	(
		cd repo &&
		cat >expect <<-EOF &&
		author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

		notes (:11)
		EOF
		git cat-file commit refs/notes/foobar2 | sed 1d >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify third notes tree' '
	setup_notes_repo &&
	(
		cd repo &&
		sort >expect <<-EOF &&
		100644 blob $commit1
		EOF
		git cat-file -p refs/notes/foobar2^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify third note for first commit' '
	setup_notes_repo &&
	(
		cd repo &&
		echo "$note1c_data" >expect &&
		git cat-file blob refs/notes/foobar2:$commit1 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify fourth notes commit' '
	setup_notes_repo &&
	(
		cd repo &&
		cat >expect <<-EOF &&
		parent $(git rev-parse --verify refs/notes/foobar^)
		author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

		notes (:12)
		EOF
		git cat-file commit refs/notes/foobar | sed 1d >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify fourth notes tree' '
	setup_notes_repo &&
	(
		cd repo &&
		sort >expect <<-EOF &&
		100644 blob $commit2
		EOF
		git cat-file -p refs/notes/foobar^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify second note for second commit' '
	setup_notes_repo &&
	(
		cd repo &&
		echo "$note2b_data" >expect &&
		git cat-file blob refs/notes/foobar:$commit2 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'deny note on empty branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		cat >input <<-EOF &&
		reset refs/heads/Q0

		commit refs/heads/note-Q0
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		Note for an empty branch.
		COMMIT

		N inline refs/heads/Q0
		data <<NOTE
		some note
		NOTE
		EOF
		test_must_fail git fast-import <input
	)
'

test_done
