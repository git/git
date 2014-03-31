#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test commit notes'

. ./test-lib.sh

cat > fake_editor.sh << \EOF
#!/bin/sh
echo "$MSG" > "$1"
echo "$MSG" >& 2
EOF
chmod a+x fake_editor.sh
GIT_EDITOR=./fake_editor.sh
export GIT_EDITOR

test_expect_success 'cannot annotate non-existing HEAD' '
	test_must_fail env MSG=3 git notes add
'

test_expect_success setup '
	: > a1 &&
	git add a1 &&
	test_tick &&
	git commit -m 1st &&
	: > a2 &&
	git add a2 &&
	test_tick &&
	git commit -m 2nd
'

test_expect_success 'need valid notes ref' '
	test_must_fail env MSG=1 GIT_NOTES_REF=/ git notes show &&
	test_must_fail env MSG=2 GIT_NOTES_REF=/ git notes show
'

test_expect_success 'refusing to add notes in refs/heads/' '
	test_must_fail env MSG=1 GIT_NOTES_REF=refs/heads/bogus git notes add
'

test_expect_success 'refusing to edit notes in refs/remotes/' '
	test_must_fail env MSG=1 GIT_NOTES_REF=refs/heads/bogus git notes edit
'

# 1 indicates caught gracefully by die, 128 means git-show barked
test_expect_success 'handle empty notes gracefully' '
	test_expect_code 1 git notes show
'

test_expect_success 'show non-existent notes entry with %N' '
	for l in A B
	do
		echo "$l"
	done >expect &&
	git show -s --format='A%n%NB' >output &&
	test_cmp expect output
'

test_expect_success 'create notes' '
	git config core.notesRef refs/notes/commits &&
	MSG=b4 git notes add &&
	test ! -f .git/NOTES_EDITMSG &&
	test 1 = $(git ls-tree refs/notes/commits | wc -l) &&
	test b4 = $(git notes show) &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'show notes entry with %N' '
	for l in A b4 B
	do
		echo "$l"
	done >expect &&
	git show -s --format='A%n%NB' >output &&
	test_cmp expect output
'

cat >expect <<EOF
d423f8c refs/notes/commits@{0}: notes: Notes added by 'git notes add'
EOF

test_expect_success 'create reflog entry' '
	git reflog show refs/notes/commits >output &&
	test_cmp expect output
'

test_expect_success 'edit existing notes' '
	MSG=b3 git notes edit &&
	test ! -f .git/NOTES_EDITMSG &&
	test 1 = $(git ls-tree refs/notes/commits | wc -l) &&
	test b3 = $(git notes show) &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'cannot "git notes add -m" where notes already exists' '
	test_must_fail git notes add -m "b2" &&
	test ! -f .git/NOTES_EDITMSG &&
	test 1 = $(git ls-tree refs/notes/commits | wc -l) &&
	test b3 = $(git notes show) &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'can overwrite existing note with "git notes add -f -m"' '
	git notes add -f -m "b1" &&
	test ! -f .git/NOTES_EDITMSG &&
	test 1 = $(git ls-tree refs/notes/commits | wc -l) &&
	test b1 = $(git notes show) &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'add w/no options on existing note morphs into edit' '
	MSG=b2 git notes add &&
	test ! -f .git/NOTES_EDITMSG &&
	test 1 = $(git ls-tree refs/notes/commits | wc -l) &&
	test b2 = $(git notes show) &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'can overwrite existing note with "git notes add -f"' '
	MSG=b1 git notes add -f &&
	test ! -f .git/NOTES_EDITMSG &&
	test 1 = $(git ls-tree refs/notes/commits | wc -l) &&
	test b1 = $(git notes show) &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

cat > expect << EOF
commit 268048bfb8a1fb38e703baceb8ab235421bf80c5
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:14:13 2005 -0700

    2nd

Notes:
    b1
EOF

test_expect_success 'show notes' '
	! (git cat-file commit HEAD | grep b1) &&
	git log -1 > output &&
	test_cmp expect output
'

test_expect_success 'create multi-line notes (setup)' '
	: > a3 &&
	git add a3 &&
	test_tick &&
	git commit -m 3rd &&
	MSG="b3
c3c3c3c3
d3d3d3" git notes add
'

cat > expect-multiline << EOF
commit 1584215f1d29c65e99c6c6848626553fdd07fd75
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:15:13 2005 -0700

    3rd

Notes:
    b3
    c3c3c3c3
    d3d3d3
EOF

printf "\n" >> expect-multiline
cat expect >> expect-multiline

test_expect_success 'show multi-line notes' '
	git log -2 > output &&
	test_cmp expect-multiline output
'
test_expect_success 'create -F notes (setup)' '
	: > a4 &&
	git add a4 &&
	test_tick &&
	git commit -m 4th &&
	echo "xyzzy" > note5 &&
	git notes add -F note5
'

cat > expect-F << EOF
commit 15023535574ded8b1a89052b32673f84cf9582b8
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:16:13 2005 -0700

    4th

Notes:
    xyzzy
EOF

printf "\n" >> expect-F
cat expect-multiline >> expect-F

test_expect_success 'show -F notes' '
	git log -3 > output &&
	test_cmp expect-F output
'

test_expect_success 'Re-adding -F notes without -f fails' '
	echo "zyxxy" > note5 &&
	test_must_fail git notes add -F note5 &&
	git log -3 > output &&
	test_cmp expect-F output
'

cat >expect << EOF
commit 15023535574ded8b1a89052b32673f84cf9582b8
tree e070e3af51011e47b183c33adf9736736a525709
parent 1584215f1d29c65e99c6c6848626553fdd07fd75
author A U Thor <author@example.com> 1112912173 -0700
committer C O Mitter <committer@example.com> 1112912173 -0700

    4th
EOF
test_expect_success 'git log --pretty=raw does not show notes' '
	git log -1 --pretty=raw >output &&
	test_cmp expect output
'

cat >>expect <<EOF

Notes:
    xyzzy
EOF
test_expect_success 'git log --show-notes' '
	git log -1 --pretty=raw --show-notes >output &&
	test_cmp expect output
'

test_expect_success 'git log --no-notes' '
	git log -1 --no-notes >output &&
	! grep xyzzy output
'

test_expect_success 'git format-patch does not show notes' '
	git format-patch -1 --stdout >output &&
	! grep xyzzy output
'

test_expect_success 'git format-patch --show-notes does show notes' '
	git format-patch --show-notes -1 --stdout >output &&
	grep xyzzy output
'

for pretty in \
	"" --pretty --pretty=raw --pretty=short --pretty=medium \
	--pretty=full --pretty=fuller --pretty=format:%s --oneline
do
	case "$pretty" in
	"") p= not= negate="" ;;
	?*) p="$pretty" not=" not" negate="!" ;;
	esac
	test_expect_success "git show $pretty does$not show notes" '
		git show $p >output &&
		eval "$negate grep xyzzy output"
	'
done

test_expect_success 'setup alternate notes ref' '
	git notes --ref=alternate add -m alternate
'

test_expect_success 'git log --notes shows default notes' '
	git log -1 --notes >output &&
	grep xyzzy output &&
	! grep alternate output
'

test_expect_success 'git log --notes=X shows only X' '
	git log -1 --notes=alternate >output &&
	! grep xyzzy output &&
	grep alternate output
'

test_expect_success 'git log --notes --notes=X shows both' '
	git log -1 --notes --notes=alternate >output &&
	grep xyzzy output &&
	grep alternate output
'

test_expect_success 'git log --no-notes resets default state' '
	git log -1 --notes --notes=alternate \
		--no-notes --notes=alternate \
		>output &&
	! grep xyzzy output &&
	grep alternate output
'

test_expect_success 'git log --no-notes resets ref list' '
	git log -1 --notes --notes=alternate \
		--no-notes --notes \
		>output &&
	grep xyzzy output &&
	! grep alternate output
'

test_expect_success 'create -m notes (setup)' '
	: > a5 &&
	git add a5 &&
	test_tick &&
	git commit -m 5th &&
	git notes add -m spam -m "foo
bar
baz"
'

whitespace="    "
cat > expect-m << EOF
commit bd1753200303d0a0344be813e504253b3d98e74d
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:17:13 2005 -0700

    5th

Notes:
    spam
$whitespace
    foo
    bar
    baz
EOF

printf "\n" >> expect-m
cat expect-F >> expect-m

test_expect_success 'show -m notes' '
	git log -4 > output &&
	test_cmp expect-m output
'

test_expect_success 'remove note with add -f -F /dev/null (setup)' '
	git notes add -f -F /dev/null
'

cat > expect-rm-F << EOF
commit bd1753200303d0a0344be813e504253b3d98e74d
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:17:13 2005 -0700

    5th
EOF

printf "\n" >> expect-rm-F
cat expect-F >> expect-rm-F

test_expect_success 'verify note removal with -F /dev/null' '
	git log -4 > output &&
	test_cmp expect-rm-F output &&
	test_must_fail git notes show
'

test_expect_success 'do not create empty note with -m "" (setup)' '
	git notes add -m ""
'

test_expect_success 'verify non-creation of note with -m ""' '
	git log -4 > output &&
	test_cmp expect-rm-F output &&
	test_must_fail git notes show
'

cat > expect-combine_m_and_F << EOF
foo

xyzzy

bar

zyxxy

baz
EOF

test_expect_success 'create note with combination of -m and -F' '
	echo "xyzzy" > note_a &&
	echo "zyxxy" > note_b &&
	git notes add -m "foo" -F note_a -m "bar" -F note_b -m "baz" &&
	git notes show > output &&
	test_cmp expect-combine_m_and_F output
'

test_expect_success 'remove note with "git notes remove" (setup)' '
	git notes remove HEAD^ &&
	git notes remove
'

cat > expect-rm-remove << EOF
commit bd1753200303d0a0344be813e504253b3d98e74d
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:17:13 2005 -0700

    5th

commit 15023535574ded8b1a89052b32673f84cf9582b8
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:16:13 2005 -0700

    4th
EOF

printf "\n" >> expect-rm-remove
cat expect-multiline >> expect-rm-remove

test_expect_success 'verify note removal with "git notes remove"' '
	git log -4 > output &&
	test_cmp expect-rm-remove output &&
	test_must_fail git notes show HEAD^
'

cat > expect << EOF
c18dc024e14f08d18d14eea0d747ff692d66d6a3 1584215f1d29c65e99c6c6848626553fdd07fd75
c9c6af7f78bc47490dbf3e822cf2f3c24d4b9061 268048bfb8a1fb38e703baceb8ab235421bf80c5
EOF

test_expect_success 'removing non-existing note should not create new commit' '
	git rev-parse --verify refs/notes/commits > before_commit &&
	test_must_fail git notes remove HEAD^ &&
	git rev-parse --verify refs/notes/commits > after_commit &&
	test_cmp before_commit after_commit
'

test_expect_success 'removing more than one' '
	before=$(git rev-parse --verify refs/notes/commits) &&
	test_when_finished "git update-ref refs/notes/commits $before" &&

	# We have only two -- add another and make sure it stays
	git notes add -m "extra" &&
	git notes list HEAD >after-removal-expect &&
	git notes remove HEAD^^ HEAD^^^ &&
	git notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'removing is atomic' '
	before=$(git rev-parse --verify refs/notes/commits) &&
	test_when_finished "git update-ref refs/notes/commits $before" &&
	test_must_fail git notes remove HEAD^^ HEAD^^^ HEAD^ &&
	after=$(git rev-parse --verify refs/notes/commits) &&
	test "$before" = "$after"
'

test_expect_success 'removing with --ignore-missing' '
	before=$(git rev-parse --verify refs/notes/commits) &&
	test_when_finished "git update-ref refs/notes/commits $before" &&

	# We have only two -- add another and make sure it stays
	git notes add -m "extra" &&
	git notes list HEAD >after-removal-expect &&
	git notes remove --ignore-missing HEAD^^ HEAD^^^ HEAD^ &&
	git notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'removing with --ignore-missing but bogus ref' '
	before=$(git rev-parse --verify refs/notes/commits) &&
	test_when_finished "git update-ref refs/notes/commits $before" &&
	test_must_fail git notes remove --ignore-missing HEAD^^ HEAD^^^ NO-SUCH-COMMIT &&
	after=$(git rev-parse --verify refs/notes/commits) &&
	test "$before" = "$after"
'

test_expect_success 'remove reads from --stdin' '
	before=$(git rev-parse --verify refs/notes/commits) &&
	test_when_finished "git update-ref refs/notes/commits $before" &&

	# We have only two -- add another and make sure it stays
	git notes add -m "extra" &&
	git notes list HEAD >after-removal-expect &&
	git rev-parse HEAD^^ HEAD^^^ >input &&
	git notes remove --stdin <input &&
	git notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'remove --stdin is also atomic' '
	before=$(git rev-parse --verify refs/notes/commits) &&
	test_when_finished "git update-ref refs/notes/commits $before" &&
	git rev-parse HEAD^^ HEAD^^^ HEAD^ >input &&
	test_must_fail git notes remove --stdin <input &&
	after=$(git rev-parse --verify refs/notes/commits) &&
	test "$before" = "$after"
'

test_expect_success 'removing with --stdin --ignore-missing' '
	before=$(git rev-parse --verify refs/notes/commits) &&
	test_when_finished "git update-ref refs/notes/commits $before" &&

	# We have only two -- add another and make sure it stays
	git notes add -m "extra" &&
	git notes list HEAD >after-removal-expect &&
	git rev-parse HEAD^^ HEAD^^^ HEAD^ >input &&
	git notes remove --ignore-missing --stdin <input &&
	git notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'list notes with "git notes list"' '
	git notes list > output &&
	test_cmp expect output
'

test_expect_success 'list notes with "git notes"' '
	git notes > output &&
	test_cmp expect output
'

cat > expect << EOF
c18dc024e14f08d18d14eea0d747ff692d66d6a3
EOF

test_expect_success 'list specific note with "git notes list <object>"' '
	git notes list HEAD^^ > output &&
	test_cmp expect output
'

cat > expect << EOF
EOF

test_expect_success 'listing non-existing notes fails' '
	test_must_fail git notes list HEAD > output &&
	test_cmp expect output
'

cat > expect << EOF
Initial set of notes

More notes appended with git notes append
EOF

test_expect_success 'append to existing note with "git notes append"' '
	git notes add -m "Initial set of notes" &&
	git notes append -m "More notes appended with git notes append" &&
	git notes show > output &&
	test_cmp expect output
'

cat > expect_list << EOF
c18dc024e14f08d18d14eea0d747ff692d66d6a3 1584215f1d29c65e99c6c6848626553fdd07fd75
c9c6af7f78bc47490dbf3e822cf2f3c24d4b9061 268048bfb8a1fb38e703baceb8ab235421bf80c5
4b6ad22357cc8a1296720574b8d2fbc22fab0671 bd1753200303d0a0344be813e504253b3d98e74d
EOF

test_expect_success '"git notes list" does not expand to "git notes list HEAD"' '
	git notes list > output &&
	test_cmp expect_list output
'

test_expect_success 'appending empty string does not change existing note' '
	git notes append -m "" &&
	git notes show > output &&
	test_cmp expect output
'

test_expect_success 'git notes append == add when there is no existing note' '
	git notes remove HEAD &&
	test_must_fail git notes list HEAD &&
	git notes append -m "Initial set of notes

More notes appended with git notes append" &&
	git notes show > output &&
	test_cmp expect output
'

test_expect_success 'appending empty string to non-existing note does not create note' '
	git notes remove HEAD &&
	test_must_fail git notes list HEAD &&
	git notes append -m "" &&
	test_must_fail git notes list HEAD
'

test_expect_success 'create other note on a different notes ref (setup)' '
	: > a6 &&
	git add a6 &&
	test_tick &&
	git commit -m 6th &&
	GIT_NOTES_REF="refs/notes/other" git notes add -m "other note"
'

cat > expect-other << EOF
commit 387a89921c73d7ed72cd94d179c1c7048ca47756
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:18:13 2005 -0700

    6th

Notes (other):
    other note
EOF

cat > expect-not-other << EOF
commit 387a89921c73d7ed72cd94d179c1c7048ca47756
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:18:13 2005 -0700

    6th
EOF

test_expect_success 'Do not show note on other ref by default' '
	git log -1 > output &&
	test_cmp expect-not-other output
'

test_expect_success 'Do show note when ref is given in GIT_NOTES_REF' '
	GIT_NOTES_REF="refs/notes/other" git log -1 > output &&
	test_cmp expect-other output
'

test_expect_success 'Do show note when ref is given in core.notesRef config' '
	git config core.notesRef "refs/notes/other" &&
	git log -1 > output &&
	test_cmp expect-other output
'

test_expect_success 'Do not show note when core.notesRef is overridden' '
	GIT_NOTES_REF="refs/notes/wrong" git log -1 > output &&
	test_cmp expect-not-other output
'

cat > expect-both << EOF
commit 387a89921c73d7ed72cd94d179c1c7048ca47756
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:18:13 2005 -0700

    6th

Notes:
    order test

Notes (other):
    other note

commit bd1753200303d0a0344be813e504253b3d98e74d
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:17:13 2005 -0700

    5th

Notes:
    replacement for deleted note
EOF

test_expect_success 'Show all notes when notes.displayRef=refs/notes/*' '
	GIT_NOTES_REF=refs/notes/commits git notes add \
		-m"replacement for deleted note" HEAD^ &&
	GIT_NOTES_REF=refs/notes/commits git notes add -m"order test" &&
	git config --unset core.notesRef &&
	git config notes.displayRef "refs/notes/*" &&
	git log -2 > output &&
	test_cmp expect-both output
'

test_expect_success 'core.notesRef is implicitly in notes.displayRef' '
	git config core.notesRef refs/notes/commits &&
	git config notes.displayRef refs/notes/other &&
	git log -2 > output &&
	test_cmp expect-both output
'

test_expect_success 'notes.displayRef can be given more than once' '
	git config --unset core.notesRef &&
	git config notes.displayRef refs/notes/commits &&
	git config --add notes.displayRef refs/notes/other &&
	git log -2 > output &&
	test_cmp expect-both output
'

cat > expect-both-reversed << EOF
commit 387a89921c73d7ed72cd94d179c1c7048ca47756
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:18:13 2005 -0700

    6th

Notes (other):
    other note

Notes:
    order test
EOF

test_expect_success 'notes.displayRef respects order' '
	git config core.notesRef refs/notes/other &&
	git config --unset-all notes.displayRef &&
	git config notes.displayRef refs/notes/commits &&
	git log -1 > output &&
	test_cmp expect-both-reversed output
'

test_expect_success 'GIT_NOTES_DISPLAY_REF works' '
	git config --unset-all core.notesRef &&
	git config --unset-all notes.displayRef &&
	GIT_NOTES_DISPLAY_REF=refs/notes/commits:refs/notes/other \
		git log -2 > output &&
	test_cmp expect-both output
'

cat > expect-none << EOF
commit 387a89921c73d7ed72cd94d179c1c7048ca47756
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:18:13 2005 -0700

    6th

commit bd1753200303d0a0344be813e504253b3d98e74d
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:17:13 2005 -0700

    5th
EOF

test_expect_success 'GIT_NOTES_DISPLAY_REF overrides config' '
	git config notes.displayRef "refs/notes/*" &&
	GIT_NOTES_REF= GIT_NOTES_DISPLAY_REF= git log -2 > output &&
	test_cmp expect-none output
'

test_expect_success '--show-notes=* adds to GIT_NOTES_DISPLAY_REF' '
	GIT_NOTES_REF= GIT_NOTES_DISPLAY_REF= git log --show-notes=* -2 > output &&
	test_cmp expect-both output
'

cat > expect-commits << EOF
commit 387a89921c73d7ed72cd94d179c1c7048ca47756
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:18:13 2005 -0700

    6th

Notes:
    order test
EOF

test_expect_success '--no-standard-notes' '
	git log --no-standard-notes --show-notes=commits -1 > output &&
	test_cmp expect-commits output
'

test_expect_success '--standard-notes' '
	git log --no-standard-notes --show-notes=commits \
		--standard-notes -2 > output &&
	test_cmp expect-both output
'

test_expect_success '--show-notes=ref accumulates' '
	git log --show-notes=other --show-notes=commits \
		 --no-standard-notes -1 > output &&
	test_cmp expect-both-reversed output
'

test_expect_success 'Allow notes on non-commits (trees, blobs, tags)' '
	git config core.notesRef refs/notes/other &&
	echo "Note on a tree" > expect &&
	git notes add -m "Note on a tree" HEAD: &&
	git notes show HEAD: > actual &&
	test_cmp expect actual &&
	echo "Note on a blob" > expect &&
	filename=$(git ls-tree --name-only HEAD | head -n1) &&
	git notes add -m "Note on a blob" HEAD:$filename &&
	git notes show HEAD:$filename > actual &&
	test_cmp expect actual &&
	echo "Note on a tag" > expect &&
	git tag -a -m "This is an annotated tag" foobar HEAD^ &&
	git notes add -m "Note on a tag" foobar &&
	git notes show foobar > actual &&
	test_cmp expect actual
'

cat > expect << EOF
commit 2ede89468182a62d0bde2583c736089bcf7d7e92
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:19:13 2005 -0700

    7th

Notes (other):
    other note
EOF

test_expect_success 'create note from other note with "git notes add -C"' '
	: > a7 &&
	git add a7 &&
	test_tick &&
	git commit -m 7th &&
	git notes add -C $(git notes list HEAD^) &&
	git log -1 > actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD^)"
'

test_expect_success 'create note from non-existing note with "git notes add -C" fails' '
	: > a8 &&
	git add a8 &&
	test_tick &&
	git commit -m 8th &&
	test_must_fail git notes add -C deadbeef &&
	test_must_fail git notes list HEAD
'

test_expect_success 'create note from non-blob with "git notes add -C" fails' '
	commit=$(git rev-parse --verify HEAD) &&
	tree=$(git rev-parse --verify HEAD:) &&
	test_must_fail git notes add -C $commit &&
	test_must_fail git notes add -C $tree &&
	test_must_fail git notes list HEAD
'

cat > expect << EOF
commit 80d796defacd5db327b7a4e50099663902fbdc5c
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:20:13 2005 -0700

    8th

Notes (other):
    This is a blob object
EOF

test_expect_success 'create note from blob with "git notes add -C" reuses blob id' '
	blob=$(echo "This is a blob object" | git hash-object -w --stdin) &&
	git notes add -C $blob &&
	git log -1 > actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$blob"
'

cat > expect << EOF
commit 016e982bad97eacdbda0fcbd7ce5b0ba87c81f1b
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:21:13 2005 -0700

    9th

Notes (other):
    yet another note
EOF

test_expect_success 'create note from other note with "git notes add -c"' '
	: > a9 &&
	git add a9 &&
	test_tick &&
	git commit -m 9th &&
	MSG="yet another note" git notes add -c $(git notes list HEAD^^) &&
	git log -1 > actual &&
	test_cmp expect actual
'

test_expect_success 'create note from non-existing note with "git notes add -c" fails' '
	: > a10 &&
	git add a10 &&
	test_tick &&
	git commit -m 10th &&
	test_must_fail env MSG="yet another note" git notes add -c deadbeef &&
	test_must_fail git notes list HEAD
'

cat > expect << EOF
commit 016e982bad97eacdbda0fcbd7ce5b0ba87c81f1b
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:21:13 2005 -0700

    9th

Notes (other):
    yet another note
$whitespace
    yet another note
EOF

test_expect_success 'append to note from other note with "git notes append -C"' '
	git notes append -C $(git notes list HEAD^) HEAD^ &&
	git log -1 HEAD^ > actual &&
	test_cmp expect actual
'

cat > expect << EOF
commit ffed603236bfa3891c49644257a83598afe8ae5a
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:22:13 2005 -0700

    10th

Notes (other):
    other note
EOF

test_expect_success 'create note from other note with "git notes append -c"' '
	MSG="other note" git notes append -c $(git notes list HEAD^) &&
	git log -1 > actual &&
	test_cmp expect actual
'

cat > expect << EOF
commit ffed603236bfa3891c49644257a83598afe8ae5a
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:22:13 2005 -0700

    10th

Notes (other):
    other note
$whitespace
    yet another note
EOF

test_expect_success 'append to note from other note with "git notes append -c"' '
	MSG="yet another note" git notes append -c $(git notes list HEAD) &&
	git log -1 > actual &&
	test_cmp expect actual
'

cat > expect << EOF
commit 6352c5e33dbcab725fe0579be16aa2ba8eb369be
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:23:13 2005 -0700

    11th

Notes (other):
    other note
$whitespace
    yet another note
EOF

test_expect_success 'copy note with "git notes copy"' '
	: > a11 &&
	git add a11 &&
	test_tick &&
	git commit -m 11th &&
	git notes copy HEAD^ HEAD &&
	git log -1 > actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD^)"
'

test_expect_success 'prevent overwrite with "git notes copy"' '
	test_must_fail git notes copy HEAD~2 HEAD &&
	git log -1 > actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD^)"
'

cat > expect << EOF
commit 6352c5e33dbcab725fe0579be16aa2ba8eb369be
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:23:13 2005 -0700

    11th

Notes (other):
    yet another note
$whitespace
    yet another note
EOF

test_expect_success 'allow overwrite with "git notes copy -f"' '
	git notes copy -f HEAD~2 HEAD &&
	git log -1 > actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD~2)"
'

test_expect_success 'cannot copy note from object without notes' '
	: > a12 &&
	git add a12 &&
	test_tick &&
	git commit -m 12th &&
	: > a13 &&
	git add a13 &&
	test_tick &&
	git commit -m 13th &&
	test_must_fail git notes copy HEAD^ HEAD
'

cat > expect << EOF
commit e5d4fb5698d564ab8c73551538ecaf2b0c666185
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:25:13 2005 -0700

    13th

Notes (other):
    yet another note
$whitespace
    yet another note

commit 7038787dfe22a14c3867ce816dbba39845359719
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:24:13 2005 -0700

    12th

Notes (other):
    other note
$whitespace
    yet another note
EOF

test_expect_success 'git notes copy --stdin' '
	(echo $(git rev-parse HEAD~3) $(git rev-parse HEAD^); \
	echo $(git rev-parse HEAD~2) $(git rev-parse HEAD)) |
	git notes copy --stdin &&
	git log -2 > output &&
	test_cmp expect output &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD~2)" &&
	test "$(git notes list HEAD^)" = "$(git notes list HEAD~3)"
'

cat > expect << EOF
commit 37a0d4cba38afef96ba54a3ea567e6dac575700b
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:27:13 2005 -0700

    15th

commit be28d8b4d9951ad940d229ee3b0b9ee3b1ec273d
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:26:13 2005 -0700

    14th
EOF

test_expect_success 'git notes copy --for-rewrite (unconfigured)' '
	test_commit 14th &&
	test_commit 15th &&
	(echo $(git rev-parse HEAD~3) $(git rev-parse HEAD^); \
	echo $(git rev-parse HEAD~2) $(git rev-parse HEAD)) |
	git notes copy --for-rewrite=foo &&
	git log -2 > output &&
	test_cmp expect output
'

cat > expect << EOF
commit 37a0d4cba38afef96ba54a3ea567e6dac575700b
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:27:13 2005 -0700

    15th

Notes (other):
    yet another note
$whitespace
    yet another note

commit be28d8b4d9951ad940d229ee3b0b9ee3b1ec273d
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:26:13 2005 -0700

    14th

Notes (other):
    other note
$whitespace
    yet another note
EOF

test_expect_success 'git notes copy --for-rewrite (enabled)' '
	git config notes.rewriteMode overwrite &&
	git config notes.rewriteRef "refs/notes/*" &&
	(echo $(git rev-parse HEAD~3) $(git rev-parse HEAD^); \
	echo $(git rev-parse HEAD~2) $(git rev-parse HEAD)) |
	git notes copy --for-rewrite=foo &&
	git log -2 > output &&
	test_cmp expect output
'

test_expect_success 'git notes copy --for-rewrite (disabled)' '
	git config notes.rewrite.bar false &&
	echo $(git rev-parse HEAD~3) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=bar &&
	git log -2 > output &&
	test_cmp expect output
'

cat > expect << EOF
commit 37a0d4cba38afef96ba54a3ea567e6dac575700b
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:27:13 2005 -0700

    15th

Notes (other):
    a fresh note
EOF

test_expect_success 'git notes copy --for-rewrite (overwrite)' '
	git notes add -f -m"a fresh note" HEAD^ &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=foo &&
	git log -1 > output &&
	test_cmp expect output
'

test_expect_success 'git notes copy --for-rewrite (ignore)' '
	git config notes.rewriteMode ignore &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=foo &&
	git log -1 > output &&
	test_cmp expect output
'

cat > expect << EOF
commit 37a0d4cba38afef96ba54a3ea567e6dac575700b
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:27:13 2005 -0700

    15th

Notes (other):
    a fresh note
$whitespace
    another fresh note
EOF

test_expect_success 'git notes copy --for-rewrite (append)' '
	git notes add -f -m"another fresh note" HEAD^ &&
	git config notes.rewriteMode concatenate &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=foo &&
	git log -1 > output &&
	test_cmp expect output
'

cat > expect << EOF
commit 37a0d4cba38afef96ba54a3ea567e6dac575700b
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:27:13 2005 -0700

    15th

Notes (other):
    a fresh note
$whitespace
    another fresh note
$whitespace
    append 1
$whitespace
    append 2
EOF

test_expect_success 'git notes copy --for-rewrite (append two to one)' '
	git notes add -f -m"append 1" HEAD^ &&
	git notes add -f -m"append 2" HEAD^^ &&
	(echo $(git rev-parse HEAD^) $(git rev-parse HEAD);
	echo $(git rev-parse HEAD^^) $(git rev-parse HEAD)) |
	git notes copy --for-rewrite=foo &&
	git log -1 > output &&
	test_cmp expect output
'

test_expect_success 'git notes copy --for-rewrite (append empty)' '
	git notes remove HEAD^ &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=foo &&
	git log -1 > output &&
	test_cmp expect output
'

cat > expect << EOF
commit 37a0d4cba38afef96ba54a3ea567e6dac575700b
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:27:13 2005 -0700

    15th

Notes (other):
    replacement note 1
EOF

test_expect_success 'GIT_NOTES_REWRITE_MODE works' '
	git notes add -f -m"replacement note 1" HEAD^ &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	GIT_NOTES_REWRITE_MODE=overwrite git notes copy --for-rewrite=foo &&
	git log -1 > output &&
	test_cmp expect output
'

cat > expect << EOF
commit 37a0d4cba38afef96ba54a3ea567e6dac575700b
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:27:13 2005 -0700

    15th

Notes (other):
    replacement note 2
EOF

test_expect_success 'GIT_NOTES_REWRITE_REF works' '
	git config notes.rewriteMode overwrite &&
	git notes add -f -m"replacement note 2" HEAD^ &&
	git config --unset-all notes.rewriteRef &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	GIT_NOTES_REWRITE_REF=refs/notes/commits:refs/notes/other \
		git notes copy --for-rewrite=foo &&
	git log -1 > output &&
	test_cmp expect output
'

test_expect_success 'GIT_NOTES_REWRITE_REF overrides config' '
	git config notes.rewriteRef refs/notes/other &&
	git notes add -f -m"replacement note 3" HEAD^ &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	GIT_NOTES_REWRITE_REF= git notes copy --for-rewrite=foo &&
	git log -1 > output &&
	test_cmp expect output
'

test_expect_success 'git notes copy diagnoses too many or too few parameters' '
	test_must_fail git notes copy &&
	test_must_fail git notes copy one two three
'

test_expect_success 'git notes get-ref (no overrides)' '
	git config --unset core.notesRef &&
	sane_unset GIT_NOTES_REF &&
	test "$(git notes get-ref)" = "refs/notes/commits"
'

test_expect_success 'git notes get-ref (core.notesRef)' '
	git config core.notesRef refs/notes/foo &&
	test "$(git notes get-ref)" = "refs/notes/foo"
'

test_expect_success 'git notes get-ref (GIT_NOTES_REF)' '
	test "$(GIT_NOTES_REF=refs/notes/bar git notes get-ref)" = "refs/notes/bar"
'

test_expect_success 'git notes get-ref (--ref)' '
	test "$(GIT_NOTES_REF=refs/notes/bar git notes --ref=baz get-ref)" = "refs/notes/baz"
'

test_done
