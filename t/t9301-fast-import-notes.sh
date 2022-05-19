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
cummit refs/heads/main
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
first cummit
cummit

M 644 inline foo
data <<EOF
file foo in first cummit
EOF

M 755 inline bar
data <<EOF
file bar in first cummit
EOF

M 644 inline baz/xyzzy
data <<EOF
file baz/xyzzy in first cummit
EOF

cummit refs/heads/main
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
second cummit
cummit

M 644 inline foo
data <<EOF
file foo in second cummit
EOF

M 755 inline baz/xyzzy
data <<EOF
file baz/xyzzy in second cummit
EOF

cummit refs/heads/main
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
third cummit
cummit

M 644 inline foo
data <<EOF
file foo in third cummit
EOF

cummit refs/heads/main
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
fourth cummit
cummit

M 755 inline bar
data <<EOF
file bar in fourth cummit
EOF

INPUT_END

test_expect_success 'set up main branch' '

	git fast-import <input &&
	git whatchanged main
'

cummit4=$(git rev-parse refs/heads/main)
cummit3=$(git rev-parse "$cummit4^")
cummit2=$(git rev-parse "$cummit4~2")
cummit1=$(git rev-parse "$cummit4~3")

test_tick
cat >input <<INPUT_END
cummit refs/notes/test
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
first notes cummit
cummit

M 644 inline $cummit1
data <<EOF
first note for first cummit
EOF

M 755 inline $cummit2
data <<EOF
first note for second cummit
EOF

INPUT_END

cat >expect <<EXPECT_END
    fourth cummit
    third cummit
    second cummit
    first note for second cummit
    first cummit
    first note for first cummit
EXPECT_END

test_expect_success 'add notes with simple M command' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
feature notes
cummit refs/notes/test
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
second notes cummit
cummit

from refs/notes/test^0
N inline $cummit3
data <<EOF
first note for third cummit
EOF

N inline $cummit4
data <<EOF
first note for fourth cummit
EOF

INPUT_END

cat >expect <<EXPECT_END
    fourth cummit
    first note for fourth cummit
    third cummit
    first note for third cummit
    second cummit
    first note for second cummit
    first cummit
    first note for first cummit
EXPECT_END

test_expect_success 'add notes with simple N command' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
cummit refs/notes/test
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
third notes cummit
cummit

from refs/notes/test^0
N inline $cummit1
data <<EOF
second note for first cummit
EOF

N inline $cummit2
data <<EOF
second note for second cummit
EOF

N inline $cummit3
data <<EOF
second note for third cummit
EOF

N inline $cummit4
data <<EOF
second note for fourth cummit
EOF

INPUT_END

cat >expect <<EXPECT_END
    fourth cummit
    second note for fourth cummit
    third cummit
    second note for third cummit
    second cummit
    second note for second cummit
    first cummit
    second note for first cummit
EXPECT_END

test_expect_success 'update existing notes with N command' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
cummit refs/notes/test
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
fourth notes cummit
cummit

from refs/notes/test^0
M 644 inline $(echo "$cummit3" | sed "s|^..|&/|")
data <<EOF
prefix of note for third cummit
EOF

M 644 inline $(echo "$cummit4" | sed "s|^..|&/|")
data <<EOF
prefix of note for fourth cummit
EOF

M 644 inline $(echo "$cummit4" | sed "s|^\(..\)\(..\)|\1/\2/|")
data <<EOF
pre-prefix of note for fourth cummit
EOF

N inline $cummit1
data <<EOF
third note for first cummit
EOF

N inline $cummit2
data <<EOF
third note for second cummit
EOF

N inline $cummit3
data <<EOF
third note for third cummit
EOF

N inline $cummit4
data <<EOF
third note for fourth cummit
EOF


INPUT_END

whitespace="    "

cat >expect <<EXPECT_END
    fourth cummit
    pre-prefix of note for fourth cummit
$whitespace
    prefix of note for fourth cummit
$whitespace
    third note for fourth cummit
    third cummit
    prefix of note for third cummit
$whitespace
    third note for third cummit
    second cummit
    third note for second cummit
    first cummit
    third note for first cummit
EXPECT_END

test_expect_success 'add concatenation notes with M command' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
cummit refs/notes/test
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
fifth notes cummit
cummit

from refs/notes/test^0
deleteall

INPUT_END

cat >expect <<EXPECT_END
    fourth cummit
    third cummit
    second cummit
    first cummit
EXPECT_END

test_expect_success 'verify that deleteall also removes notes' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

test_tick
cat >input <<INPUT_END
cummit refs/notes/test
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
sixth notes cummit
cummit

from refs/notes/test^0
M 644 inline $cummit1
data <<EOF
third note for first cummit
EOF

M 644 inline $cummit3
data <<EOF
third note for third cummit
EOF

N inline $cummit1
data <<EOF
fourth note for first cummit
EOF

N inline $cummit3
data <<EOF
fourth note for third cummit
EOF

INPUT_END

cat >expect <<EXPECT_END
    fourth cummit
    third cummit
    fourth note for third cummit
    second cummit
    first cummit
    fourth note for first cummit
EXPECT_END

test_expect_success 'verify that later N commands override earlier M commands' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/test git log | grep "^    " > actual &&
	test_cmp expect actual

'

# Write fast-import commands to create the given number of cummits
fast_import_cummits () {
	my_ref=$1
	my_num_cummits=$2
	my_append_to_file=$3
	my_i=0
	while test $my_i -lt $my_num_cummits
	do
		my_i=$(($my_i + 1))
		test_tick
		cat >>"$my_append_to_file" <<INPUT_END
cummit $my_ref
mark :$my_i
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
cummit #$my_i
cummit

M 644 inline file
data <<EOF
file contents in cummit #$my_i
EOF

INPUT_END
	done
}

# Write fast-import commands to create the given number of notes annotating
# the cummits created by fast_import_cummits()
fast_import_notes () {
	my_notes_ref=$1
	my_num_cummits=$2
	my_append_to_file=$3
	my_note_append=$4
	test_tick
	cat >>"$my_append_to_file" <<INPUT_END
cummit $my_notes_ref
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
cummitting $my_num_cummits notes
cummit

INPUT_END

	my_i=0
	while test $my_i -lt $my_num_cummits
	do
		my_i=$(($my_i + 1))
		cat >>"$my_append_to_file" <<INPUT_END
N inline :$my_i
data <<EOF
note for cummit #$my_i$my_note_append
EOF

INPUT_END
	done
}


rm input expect
num_cummits=400
# Create lots of cummits
fast_import_cummits "refs/heads/many_cummits" $num_cummits input
# Create one note per above cummit
fast_import_notes "refs/notes/many_notes" $num_cummits input
# Add a couple of non-notes as well
test_tick
cat >>input <<INPUT_END
cummit refs/notes/many_notes
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
cummitting some non-notes to the notes tree
cummit

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
# Finally create the expected output from all these notes and cummits
i=$num_cummits
while test $i -gt 0
do
	cat >>expect <<EXPECT_END
    cummit #$i
    note for cummit #$i
EXPECT_END
	i=$(($i - 1))
done

test_expect_success 'add lots of cummits and notes' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/many_notes git log refs/heads/many_cummits |
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
cummit refs/heads/other_cummits
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
cummit #$(($num_cummit + 1))
cummit

from refs/heads/many_cummits
M 644 inline file
data <<EOF
file contents in cummit #$(($num_cummit + 1))
EOF

cummit refs/notes/other_notes
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
cummitting one more note on a tree imported from a previous notes tree
cummit

M 040000 $(git log --no-walk --format=%T refs/notes/many_notes)$SP
N inline :$(($num_cummit + 1))
data <<EOF
note for cummit #$(($num_cummit + 1))
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

# Change the notes for the three top cummits
test_tick
cat >input <<INPUT_END
cummit refs/notes/many_notes
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
changing notes for the top three cummits
cummit
from refs/notes/many_notes^0
INPUT_END

rm expect
i=$num_cummits
j=0
while test $j -lt 3
do
	cat >>input <<INPUT_END
N inline refs/heads/many_cummits~$j
data <<EOF
changed note for cummit #$i
EOF
INPUT_END
	cat >>expect <<EXPECT_END
    cummit #$i
    changed note for cummit #$i
EXPECT_END
	i=$(($i - 1))
	j=$(($j + 1))
done

test_expect_success 'change a few existing notes' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/many_notes git log -n3 refs/heads/many_cummits |
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
cummit refs/notes/many_notes
cummitter $GIT_cummitTER_NAME <$GIT_cummitTER_EMAIL> $GIT_cummitTER_DATE
data <<cummit
removing all notes but $remaining_notes
cummit
from refs/notes/many_notes^0
INPUT_END

i=$(($num_cummits - $remaining_notes))
for sha1 in $(git rev-list -n $i refs/heads/many_cummits)
do
	cat >>input <<INPUT_END
N $ZERO_OID $sha1
INPUT_END
done

i=$num_cummits
rm expect
while test $i -gt 0
do
	cat >>expect <<EXPECT_END
    cummit #$i
EXPECT_END
	if test $i -le $remaining_notes
	then
		cat >>expect <<EXPECT_END
    note for cummit #$i
EXPECT_END
	fi
	i=$(($i - 1))
done

test_expect_success 'remove lots of notes' '

	git fast-import <input &&
	GIT_NOTES_REF=refs/notes/many_notes git log refs/heads/many_cummits |
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
num_cummits=16
some_cummits=8
# Create cummits
fast_import_cummits "refs/heads/more_cummits" $num_cummits input
# Create one note per above cummit per notes ref
i=0
while test $i -lt $num_notes_refs
do
	i=$(($i + 1))
	fast_import_notes "refs/notes/more_notes_$i" $num_cummits input
done
# Trigger branch reloading in git-fast-import by repeating the note creation
i=0
while test $i -lt $num_notes_refs
do
	i=$(($i + 1))
	fast_import_notes "refs/notes/more_notes_$i" $some_cummits input " (2)"
done
# Finally create the expected output from the notes in refs/notes/more_notes_1
i=$num_cummits
while test $i -gt 0
do
	note_data="note for cummit #$i"
	if test $i -le $some_cummits
	then
		note_data="$note_data (2)"
	fi
	cat >>expect <<EXPECT_END
    cummit #$i
    $note_data
EXPECT_END
	i=$(($i - 1))
done

test_expect_success "add notes to $num_cummits cummits in each of $num_notes_refs refs" '

	git fast-import --active-branches=5 <input &&
	GIT_NOTES_REF=refs/notes/more_notes_1 git log refs/heads/more_cummits |
	    grep "^    " > actual &&
	test_cmp expect actual

'

test_done
