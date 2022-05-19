#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test cummit notes'

. ./test-lib.sh

write_script fake_editor <<\EOF
echo "$MSG" >"$1"
echo "$MSG" >&2
EOF
GIT_EDITOR=./fake_editor
export GIT_EDITOR

indent="    "

test_expect_success 'cannot annotate non-existing HEAD' '
	test_must_fail env MSG=3 git notes add
'

test_expect_success 'setup' '
	test_cummit 1st &&
	test_cummit 2nd
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
	test_write_lines A B >expect &&
	git show -s --format="A%n%NB" >actual &&
	test_cmp expect actual
'

test_expect_success 'create notes' '
	MSG=b4 git notes add &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b4 >expect &&
	git notes show >actual &&
	test_cmp expect actual &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'show notes entry with %N' '
	test_write_lines A b4 B >expect &&
	git show -s --format="A%n%NB" >actual &&
	test_cmp expect actual
'

test_expect_success 'create reflog entry' '
	ref=$(git rev-parse --short refs/notes/cummits) &&
	cat <<-EOF >expect &&
		$ref refs/notes/cummits@{0}: notes: Notes added by '\''git notes add'\''
	EOF
	git reflog show refs/notes/cummits >actual &&
	test_cmp expect actual
'

test_expect_success 'edit existing notes' '
	MSG=b3 git notes edit &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b3 >expect &&
	git notes show >actual &&
	test_cmp expect actual &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'show notes from treeish' '
	echo b3 >expect &&
	git notes --ref cummits^{tree} show >actual &&
	test_cmp expect actual &&

	echo b4 >expect &&
	git notes --ref cummits@{1} show >actual &&
	test_cmp expect actual
'

test_expect_success 'cannot edit notes from non-ref' '
	test_must_fail git notes --ref cummits^{tree} edit &&
	test_must_fail git notes --ref cummits@{1} edit
'

test_expect_success 'cannot "git notes add -m" where notes already exists' '
	test_must_fail git notes add -m "b2" &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b3 >expect &&
	git notes show >actual &&
	test_cmp expect actual &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'can overwrite existing note with "git notes add -f -m"' '
	git notes add -f -m "b1" &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b1 >expect &&
	git notes show >actual &&
	test_cmp expect actual &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'add w/no options on existing note morphs into edit' '
	MSG=b2 git notes add &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b2 >expect &&
	git notes show >actual &&
	test_cmp expect actual &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'can overwrite existing note with "git notes add -f"' '
	MSG=b1 git notes add -f &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b1 >expect &&
	git notes show >actual &&
	test_cmp expect actual &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'show notes' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:14:13 2005 -0700

		${indent}2nd

		Notes:
		${indent}b1
	EOF
	git cat-file commit HEAD >cummits &&
	! grep b1 cummits &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'show multi-line notes' '
	test_cummit 3rd &&
	MSG="b3${LF}c3c3c3c3${LF}d3d3d3" git notes add &&
	cummit=$(git rev-parse HEAD) &&
	cat >expect-multiline <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:15:13 2005 -0700

		${indent}3rd

		Notes:
		${indent}b3
		${indent}c3c3c3c3
		${indent}d3d3d3

	EOF
	cat expect >>expect-multiline &&
	git log -2 >actual &&
	test_cmp expect-multiline actual
'

test_expect_success 'show -F notes' '
	test_cummit 4th &&
	echo "xyzzy" >note5 &&
	git notes add -F note5 &&
	cummit=$(git rev-parse HEAD) &&
	cat >expect-F <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:16:13 2005 -0700

		${indent}4th

		Notes:
		${indent}xyzzy

	EOF
	cat expect-multiline >>expect-F &&
	git log -3 >actual &&
	test_cmp expect-F actual
'

test_expect_success 'Re-adding -F notes without -f fails' '
	echo "zyxxy" >note5 &&
	test_must_fail git notes add -F note5 &&
	git log -3 >actual &&
	test_cmp expect-F actual
'

test_expect_success 'git log --pretty=raw does not show notes' '
	cummit=$(git rev-parse HEAD) &&
	tree=$(git rev-parse HEAD^{tree}) &&
	parent=$(git rev-parse HEAD^) &&
	cat >expect <<-EOF &&
		cummit $cummit
		tree $tree
		parent $parent
		author A U Thor <author@example.com> 1112912173 -0700
		cummitter C O Mitter <cummitter@example.com> 1112912173 -0700

		${indent}4th
	EOF
	git log -1 --pretty=raw >actual &&
	test_cmp expect actual
'

test_expect_success 'git log --show-notes' '
	cat >>expect <<-EOF &&

	Notes:
	${indent}xyzzy
	EOF
	git log -1 --pretty=raw --show-notes >actual &&
	test_cmp expect actual
'

test_expect_success 'git log --no-notes' '
	git log -1 --no-notes >actual &&
	! grep xyzzy actual
'

test_expect_success 'git format-patch does not show notes' '
	git format-patch -1 --stdout >actual &&
	! grep xyzzy actual
'

test_expect_success 'git format-patch --show-notes does show notes' '
	git format-patch --show-notes -1 --stdout >actual &&
	grep xyzzy actual
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
		git show $p >actual &&
		eval "$negate grep xyzzy actual"
	'
done

test_expect_success 'setup alternate notes ref' '
	git notes --ref=alternate add -m alternate
'

test_expect_success 'git log --notes shows default notes' '
	git log -1 --notes >actual &&
	grep xyzzy actual &&
	! grep alternate actual
'

test_expect_success 'git log --notes=X shows only X' '
	git log -1 --notes=alternate >actual &&
	! grep xyzzy actual &&
	grep alternate actual
'

test_expect_success 'git log --notes --notes=X shows both' '
	git log -1 --notes --notes=alternate >actual &&
	grep xyzzy actual &&
	grep alternate actual
'

test_expect_success 'git log --no-notes resets default state' '
	git log -1 --notes --notes=alternate \
		--no-notes --notes=alternate \
		>actual &&
	! grep xyzzy actual &&
	grep alternate actual
'

test_expect_success 'git log --no-notes resets ref list' '
	git log -1 --notes --notes=alternate \
		--no-notes --notes \
		>actual &&
	grep xyzzy actual &&
	! grep alternate actual
'

test_expect_success 'show -m notes' '
	test_cummit 5th &&
	git notes add -m spam -m "foo${LF}bar${LF}baz" &&
	cummit=$(git rev-parse HEAD) &&
	cat >expect-m <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:17:13 2005 -0700

		${indent}5th

		Notes:
		${indent}spam
		${indent}
		${indent}foo
		${indent}bar
		${indent}baz

	EOF
	cat expect-F >>expect-m &&
	git log -4 >actual &&
	test_cmp expect-m actual
'

test_expect_success 'remove note with add -f -F /dev/null' '
	git notes add -f -F /dev/null &&
	cummit=$(git rev-parse HEAD) &&
	cat >expect-rm-F <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:17:13 2005 -0700

		${indent}5th

	EOF
	cat expect-F >>expect-rm-F &&
	git log -4 >actual &&
	test_cmp expect-rm-F actual &&
	test_must_fail git notes show
'

test_expect_success 'do not create empty note with -m ""' '
	git notes add -m "" &&
	git log -4 >actual &&
	test_cmp expect-rm-F actual &&
	test_must_fail git notes show
'

test_expect_success 'create note with combination of -m and -F' '
	cat >expect-combine_m_and_F <<-EOF &&
		foo

		xyzzy

		bar

		zyxxy

		baz
	EOF
	echo "xyzzy" >note_a &&
	echo "zyxxy" >note_b &&
	git notes add -m "foo" -F note_a -m "bar" -F note_b -m "baz" &&
	git notes show >actual &&
	test_cmp expect-combine_m_and_F actual
'

test_expect_success 'remove note with "git notes remove"' '
	git notes remove HEAD^ &&
	git notes remove &&
	cummit=$(git rev-parse HEAD) &&
	parent=$(git rev-parse HEAD^) &&
	cat >expect-rm-remove <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:17:13 2005 -0700

		${indent}5th

		cummit $parent
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:16:13 2005 -0700

		${indent}4th

	EOF
	cat expect-multiline >>expect-rm-remove &&
	git log -4 >actual &&
	test_cmp expect-rm-remove actual &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'removing non-existing note should not create new cummit' '
	git rev-parse --verify refs/notes/cummits >before_cummit &&
	test_must_fail git notes remove HEAD^ &&
	git rev-parse --verify refs/notes/cummits >after_cummit &&
	test_cmp before_cummit after_cummit
'

test_expect_success 'removing more than one' '
	before=$(git rev-parse --verify refs/notes/cummits) &&
	test_when_finished "git update-ref refs/notes/cummits $before" &&

	# We have only two -- add another and make sure it stays
	git notes add -m "extra" &&
	git notes list HEAD >after-removal-expect &&
	git notes remove HEAD^^ HEAD^^^ &&
	git notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'removing is atomic' '
	before=$(git rev-parse --verify refs/notes/cummits) &&
	test_when_finished "git update-ref refs/notes/cummits $before" &&
	test_must_fail git notes remove HEAD^^ HEAD^^^ HEAD^ &&
	after=$(git rev-parse --verify refs/notes/cummits) &&
	test "$before" = "$after"
'

test_expect_success 'removing with --ignore-missing' '
	before=$(git rev-parse --verify refs/notes/cummits) &&
	test_when_finished "git update-ref refs/notes/cummits $before" &&

	# We have only two -- add another and make sure it stays
	git notes add -m "extra" &&
	git notes list HEAD >after-removal-expect &&
	git notes remove --ignore-missing HEAD^^ HEAD^^^ HEAD^ &&
	git notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'removing with --ignore-missing but bogus ref' '
	before=$(git rev-parse --verify refs/notes/cummits) &&
	test_when_finished "git update-ref refs/notes/cummits $before" &&
	test_must_fail git notes remove --ignore-missing HEAD^^ HEAD^^^ NO-SUCH-cummit &&
	after=$(git rev-parse --verify refs/notes/cummits) &&
	test "$before" = "$after"
'

test_expect_success 'remove reads from --stdin' '
	before=$(git rev-parse --verify refs/notes/cummits) &&
	test_when_finished "git update-ref refs/notes/cummits $before" &&

	# We have only two -- add another and make sure it stays
	git notes add -m "extra" &&
	git notes list HEAD >after-removal-expect &&
	git rev-parse HEAD^^ HEAD^^^ >input &&
	git notes remove --stdin <input &&
	git notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'remove --stdin is also atomic' '
	before=$(git rev-parse --verify refs/notes/cummits) &&
	test_when_finished "git update-ref refs/notes/cummits $before" &&
	git rev-parse HEAD^^ HEAD^^^ HEAD^ >input &&
	test_must_fail git notes remove --stdin <input &&
	after=$(git rev-parse --verify refs/notes/cummits) &&
	test "$before" = "$after"
'

test_expect_success 'removing with --stdin --ignore-missing' '
	before=$(git rev-parse --verify refs/notes/cummits) &&
	test_when_finished "git update-ref refs/notes/cummits $before" &&

	# We have only two -- add another and make sure it stays
	git notes add -m "extra" &&
	git notes list HEAD >after-removal-expect &&
	git rev-parse HEAD^^ HEAD^^^ HEAD^ >input &&
	git notes remove --ignore-missing --stdin <input &&
	git notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'list notes with "git notes list"' '
	cummit_2=$(git rev-parse 2nd) &&
	cummit_3=$(git rev-parse 3rd) &&
	note_2=$(git rev-parse refs/notes/cummits:$cummit_2) &&
	note_3=$(git rev-parse refs/notes/cummits:$cummit_3) &&
	sort -t" " -k2 >expect <<-EOF &&
		$note_2 $cummit_2
		$note_3 $cummit_3
	EOF
	git notes list >actual &&
	test_cmp expect actual
'

test_expect_success 'list notes with "git notes"' '
	git notes >actual &&
	test_cmp expect actual
'

test_expect_success 'list specific note with "git notes list <object>"' '
	git rev-parse refs/notes/cummits:$cummit_3 >expect &&
	git notes list HEAD^^ >actual &&
	test_cmp expect actual
'

test_expect_success 'listing non-existing notes fails' '
	test_must_fail git notes list HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'append to existing note with "git notes append"' '
	cat >expect <<-EOF &&
		Initial set of notes

		More notes appended with git notes append
	EOF
	git notes add -m "Initial set of notes" &&
	git notes append -m "More notes appended with git notes append" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success '"git notes list" does not expand to "git notes list HEAD"' '
	cummit_5=$(git rev-parse 5th) &&
	note_5=$(git rev-parse refs/notes/cummits:$cummit_5) &&
	sort -t" " -k2 >expect_list <<-EOF &&
		$note_2 $cummit_2
		$note_3 $cummit_3
		$note_5 $cummit_5
	EOF
	git notes list >actual &&
	test_cmp expect_list actual
'

test_expect_success 'appending empty string does not change existing note' '
	git notes append -m "" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes append == add when there is no existing note' '
	git notes remove HEAD &&
	test_must_fail git notes list HEAD &&
	git notes append -m "Initial set of notes${LF}${LF}More notes appended with git notes append" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'appending empty string to non-existing note does not create note' '
	git notes remove HEAD &&
	test_must_fail git notes list HEAD &&
	git notes append -m "" &&
	test_must_fail git notes list HEAD
'

test_expect_success 'create other note on a different notes ref (setup)' '
	test_cummit 6th &&
	GIT_NOTES_REF="refs/notes/other" git notes add -m "other note" &&
	cummit=$(git rev-parse HEAD) &&
	cat >expect-not-other <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th
	EOF
	cp expect-not-other expect-other &&
	cat >>expect-other <<-EOF

		Notes (other):
		${indent}other note
	EOF
'

test_expect_success 'Do not show note on other ref by default' '
	git log -1 >actual &&
	test_cmp expect-not-other actual
'

test_expect_success 'Do show note when ref is given in GIT_NOTES_REF' '
	GIT_NOTES_REF="refs/notes/other" git log -1 >actual &&
	test_cmp expect-other actual
'

test_expect_success 'Do show note when ref is given in core.notesRef config' '
	test_config core.notesRef "refs/notes/other" &&
	git log -1 >actual &&
	test_cmp expect-other actual
'

test_expect_success 'Do not show note when core.notesRef is overridden' '
	test_config core.notesRef "refs/notes/other" &&
	GIT_NOTES_REF="refs/notes/wrong" git log -1 >actual &&
	test_cmp expect-not-other actual
'

test_expect_success 'Show all notes when notes.displayRef=refs/notes/*' '
	cummit=$(git rev-parse HEAD) &&
	parent=$(git rev-parse HEAD^) &&
	cat >expect-both <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th

		Notes:
		${indent}order test

		Notes (other):
		${indent}other note

		cummit $parent
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:17:13 2005 -0700

		${indent}5th

		Notes:
		${indent}replacement for deleted note
	EOF
	GIT_NOTES_REF=refs/notes/cummits git notes add \
		-m"replacement for deleted note" HEAD^ &&
	GIT_NOTES_REF=refs/notes/cummits git notes add -m"order test" &&
	test_unconfig core.notesRef &&
	test_config notes.displayRef "refs/notes/*" &&
	git log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'core.notesRef is implicitly in notes.displayRef' '
	test_config core.notesRef refs/notes/cummits &&
	test_config notes.displayRef refs/notes/other &&
	git log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'notes.displayRef can be given more than once' '
	test_unconfig core.notesRef &&
	test_config notes.displayRef refs/notes/cummits &&
	git config --add notes.displayRef refs/notes/other &&
	git log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'notes.displayRef respects order' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect-both-reversed <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th

		Notes (other):
		${indent}other note

		Notes:
		${indent}order test
	EOF
	test_config core.notesRef refs/notes/other &&
	test_config notes.displayRef refs/notes/cummits &&
	git log -1 >actual &&
	test_cmp expect-both-reversed actual
'

test_expect_success 'notes.displayRef with no value handled gracefully' '
	test_must_fail git -c notes.displayRef log -0 --notes &&
	test_must_fail git -c notes.displayRef diff-tree --notes HEAD
'

test_expect_success 'GIT_NOTES_DISPLAY_REF works' '
	GIT_NOTES_DISPLAY_REF=refs/notes/cummits:refs/notes/other \
		git log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'GIT_NOTES_DISPLAY_REF overrides config' '
	cummit=$(git rev-parse HEAD) &&
	parent=$(git rev-parse HEAD^) &&
	cat >expect-none <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th

		cummit $parent
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:17:13 2005 -0700

		${indent}5th
	EOF
	test_config notes.displayRef "refs/notes/*" &&
	GIT_NOTES_REF= GIT_NOTES_DISPLAY_REF= git log -2 >actual &&
	test_cmp expect-none actual
'

test_expect_success '--show-notes=* adds to GIT_NOTES_DISPLAY_REF' '
	GIT_NOTES_REF= GIT_NOTES_DISPLAY_REF= git log --show-notes=* -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success '--no-standard-notes' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect-cummits <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th

		Notes:
		${indent}order test
	EOF
	git log --no-standard-notes --show-notes=cummits -1 >actual &&
	test_cmp expect-cummits actual
'

test_expect_success '--standard-notes' '
	test_config notes.displayRef "refs/notes/*" &&
	git log --no-standard-notes --show-notes=cummits \
		--standard-notes -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success '--show-notes=ref accumulates' '
	git log --show-notes=other --show-notes=cummits \
		 --no-standard-notes -1 >actual &&
	test_cmp expect-both-reversed actual
'

test_expect_success 'Allow notes on non-cummits (trees, blobs, tags)' '
	test_config core.notesRef refs/notes/other &&
	echo "Note on a tree" >expect &&
	git notes add -m "Note on a tree" HEAD: &&
	git notes show HEAD: >actual &&
	test_cmp expect actual &&
	echo "Note on a blob" >expect &&
	git ls-tree --name-only HEAD >files &&
	filename=$(head -n1 files) &&
	git notes add -m "Note on a blob" HEAD:$filename &&
	git notes show HEAD:$filename >actual &&
	test_cmp expect actual &&
	echo "Note on a tag" >expect &&
	git tag -a -m "This is an annotated tag" foobar HEAD^ &&
	git notes add -m "Note on a tag" foobar &&
	git notes show foobar >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from other note with "git notes add -C"' '
	test_cummit 7th &&
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:19:13 2005 -0700

		${indent}7th

		Notes:
		${indent}order test
	EOF
	note=$(git notes list HEAD^) &&
	git notes add -C $note &&
	git log -1 >actual &&
	test_cmp expect actual &&
	git notes list HEAD^ >expect &&
	git notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from non-existing note with "git notes add -C" fails' '
	test_cummit 8th &&
	test_must_fail git notes add -C deadbeef &&
	test_must_fail git notes list HEAD
'

test_expect_success 'create note from non-blob with "git notes add -C" fails' '
	cummit=$(git rev-parse --verify HEAD) &&
	tree=$(git rev-parse --verify HEAD:) &&
	test_must_fail git notes add -C $cummit &&
	test_must_fail git notes add -C $tree &&
	test_must_fail git notes list HEAD
'

test_expect_success 'create note from blob with "git notes add -C" reuses blob id' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:20:13 2005 -0700

		${indent}8th

		Notes:
		${indent}This is a blob object
	EOF
	echo "This is a blob object" | git hash-object -w --stdin >blob &&
	git notes add -C $(cat blob) &&
	git log -1 >actual &&
	test_cmp expect actual &&
	git notes list HEAD >actual &&
	test_cmp blob actual
'

test_expect_success 'create note from other note with "git notes add -c"' '
	test_cummit 9th &&
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:21:13 2005 -0700

		${indent}9th

		Notes:
		${indent}yet another note
	EOF
	note=$(git notes list HEAD^^) &&
	MSG="yet another note" git notes add -c $note &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from non-existing note with "git notes add -c" fails' '
	test_cummit 10th &&
	test_must_fail env MSG="yet another note" git notes add -c deadbeef &&
	test_must_fail git notes list HEAD
'

test_expect_success 'append to note from other note with "git notes append -C"' '
	cummit=$(git rev-parse HEAD^) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:21:13 2005 -0700

		${indent}9th

		Notes:
		${indent}yet another note
		${indent}
		${indent}yet another note
	EOF
	note=$(git notes list HEAD^) &&
	git notes append -C $note HEAD^ &&
	git log -1 HEAD^ >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from other note with "git notes append -c"' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:22:13 2005 -0700

		${indent}10th

		Notes:
		${indent}other note
	EOF
	note=$(git notes list HEAD^) &&
	MSG="other note" git notes append -c $note &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'append to note from other note with "git notes append -c"' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:22:13 2005 -0700

		${indent}10th

		Notes:
		${indent}other note
		${indent}
		${indent}yet another note
	EOF
	note=$(git notes list HEAD) &&
	MSG="yet another note" git notes append -c $note &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'copy note with "git notes copy"' '
	cummit=$(git rev-parse 4th) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:16:13 2005 -0700

		${indent}4th

		Notes:
		${indent}This is a blob object
	EOF
	git notes copy 8th 4th &&
	git log 3rd..4th >actual &&
	test_cmp expect actual &&
	git notes list 4th >expect &&
	git notes list 8th >actual &&
	test_cmp expect actual
'

test_expect_success 'copy note with "git notes copy" with default' '
	test_cummit 11th &&
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:23:13 2005 -0700

		${indent}11th

		Notes:
		${indent}other note
		${indent}
		${indent}yet another note
	EOF
	git notes copy HEAD^ &&
	git log -1 >actual &&
	test_cmp expect actual &&
	git notes list HEAD^ >expect &&
	git notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'prevent overwrite with "git notes copy"' '
	test_must_fail git notes copy HEAD~2 HEAD &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:23:13 2005 -0700

		${indent}11th

		Notes:
		${indent}other note
		${indent}
		${indent}yet another note
	EOF
	git log -1 >actual &&
	test_cmp expect actual &&
	git notes list HEAD^ >expect &&
	git notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'allow overwrite with "git notes copy -f"' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:23:13 2005 -0700

		${indent}11th

		Notes:
		${indent}This is a blob object
	EOF
	git notes copy -f HEAD~3 HEAD &&
	git log -1 >actual &&
	test_cmp expect actual &&
	git notes list HEAD~3 >expect &&
	git notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'allow overwrite with "git notes copy -f" with default' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:23:13 2005 -0700

		${indent}11th

		Notes:
		${indent}yet another note
		${indent}
		${indent}yet another note
	EOF
	git notes copy -f HEAD~2 &&
	git log -1 >actual &&
	test_cmp expect actual &&
	git notes list HEAD~2 >expect &&
	git notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'cannot copy note from object without notes' '
	test_cummit 12th &&
	test_cummit 13th &&
	test_must_fail git notes copy HEAD^ HEAD
'

test_expect_success 'git notes copy --stdin' '
	cummit=$(git rev-parse HEAD) &&
	parent=$(git rev-parse HEAD^) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:25:13 2005 -0700

		${indent}13th

		Notes:
		${indent}yet another note
		${indent}
		${indent}yet another note

		cummit $parent
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:24:13 2005 -0700

		${indent}12th

		Notes:
		${indent}other note
		${indent}
		${indent}yet another note
	EOF
	from=$(git rev-parse HEAD~3) &&
	to=$(git rev-parse HEAD^) &&
	echo "$from" "$to" >copy &&
	from=$(git rev-parse HEAD~2) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >>copy &&
	git notes copy --stdin <copy &&
	git log -2 >actual &&
	test_cmp expect actual &&
	git notes list HEAD~2 >expect &&
	git notes list HEAD >actual &&
	test_cmp expect actual &&
	git notes list HEAD~3 >expect &&
	git notes list HEAD^ >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (unconfigured)' '
	test_cummit 14th &&
	test_cummit 15th &&
	cummit=$(git rev-parse HEAD) &&
	parent=$(git rev-parse HEAD^) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		cummit $parent
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:26:13 2005 -0700

		${indent}14th
	EOF
	from=$(git rev-parse HEAD~3) &&
	to=$(git rev-parse HEAD^) &&
	echo "$from" "$to" >copy &&
	from=$(git rev-parse HEAD~2) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >>copy &&
	git notes copy --for-rewrite=foo <copy &&
	git log -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (enabled)' '
	cummit=$(git rev-parse HEAD) &&
	parent=$(git rev-parse HEAD^) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}yet another note
		${indent}
		${indent}yet another note

		cummit $parent
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:26:13 2005 -0700

		${indent}14th

		Notes:
		${indent}other note
		${indent}
		${indent}yet another note
	EOF
	test_config notes.rewriteMode overwrite &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(git rev-parse HEAD~3) &&
	to=$(git rev-parse HEAD^) &&
	echo "$from" "$to" >copy &&
	from=$(git rev-parse HEAD~2) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >>copy &&
	git notes copy --for-rewrite=foo <copy &&
	git log -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (disabled)' '
	test_config notes.rewrite.bar false &&
	from=$(git rev-parse HEAD~3) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	git notes copy --for-rewrite=bar <copy &&
	git log -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (overwrite)' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}a fresh note
	EOF
	git notes add -f -m"a fresh note" HEAD^ &&
	test_config notes.rewriteMode overwrite &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(git rev-parse HEAD^) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	git notes copy --for-rewrite=foo <copy &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (ignore)' '
	test_config notes.rewriteMode ignore &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(git rev-parse HEAD^) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	git notes copy --for-rewrite=foo <copy &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (append)' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}a fresh note
		${indent}
		${indent}another fresh note
	EOF
	git notes add -f -m"another fresh note" HEAD^ &&
	test_config notes.rewriteMode concatenate &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(git rev-parse HEAD^) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	git notes copy --for-rewrite=foo <copy &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (append two to one)' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}a fresh note
		${indent}
		${indent}another fresh note
		${indent}
		${indent}append 1
		${indent}
		${indent}append 2
	EOF
	git notes add -f -m"append 1" HEAD^ &&
	git notes add -f -m"append 2" HEAD^^ &&
	test_config notes.rewriteMode concatenate &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(git rev-parse HEAD^) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	from=$(git rev-parse HEAD^^) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >>copy &&
	git notes copy --for-rewrite=foo <copy &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (append empty)' '
	git notes remove HEAD^ &&
	test_config notes.rewriteMode concatenate &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(git rev-parse HEAD^) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	git notes copy --for-rewrite=foo <copy &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_NOTES_REWRITE_MODE works' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}replacement note 1
	EOF
	test_config notes.rewriteMode concatenate &&
	test_config notes.rewriteRef "refs/notes/*" &&
	git notes add -f -m"replacement note 1" HEAD^ &&
	from=$(git rev-parse HEAD^) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	GIT_NOTES_REWRITE_MODE=overwrite git notes copy --for-rewrite=foo <copy &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_NOTES_REWRITE_REF works' '
	cummit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}replacement note 2
	EOF
	git notes add -f -m"replacement note 2" HEAD^ &&
	test_config notes.rewriteMode overwrite &&
	test_unconfig notes.rewriteRef &&
	from=$(git rev-parse HEAD^) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	GIT_NOTES_REWRITE_REF=refs/notes/cummits:refs/notes/other \
		git notes copy --for-rewrite=foo <copy &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_NOTES_REWRITE_REF overrides config' '
	git notes add -f -m"replacement note 3" HEAD^ &&
	test_config notes.rewriteMode overwrite &&
	test_config notes.rewriteRef refs/notes/other &&
	from=$(git rev-parse HEAD^) &&
	to=$(git rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	GIT_NOTES_REWRITE_REF=refs/notes/cummits \
		git notes copy --for-rewrite=foo <copy &&
	git log -1 >actual &&
	grep "replacement note 3" actual
'

test_expect_success 'git notes copy diagnoses too many or too few arguments' '
	test_must_fail git notes copy 2>error &&
	test_i18ngrep "too few arguments" error &&
	test_must_fail git notes copy one two three 2>error &&
	test_i18ngrep "too many arguments" error
'

test_expect_success 'git notes get-ref expands refs/heads/main to refs/notes/refs/heads/main' '
	test_unconfig core.notesRef &&
	sane_unset GIT_NOTES_REF &&
	echo refs/notes/refs/heads/main >expect &&
	git notes --ref=refs/heads/main get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes get-ref (no overrides)' '
	test_unconfig core.notesRef &&
	sane_unset GIT_NOTES_REF &&
	echo refs/notes/cummits >expect &&
	git notes get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes get-ref (core.notesRef)' '
	test_config core.notesRef refs/notes/foo &&
	echo refs/notes/foo >expect &&
	git notes get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes get-ref (GIT_NOTES_REF)' '
	echo refs/notes/bar >expect &&
	GIT_NOTES_REF=refs/notes/bar git notes get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes get-ref (--ref)' '
	echo refs/notes/baz >expect &&
	GIT_NOTES_REF=refs/notes/bar git notes --ref=baz get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'setup testing of empty notes' '
	test_unconfig core.notesRef &&
	test_cummit 16th &&
	empty_blob=$(git hash-object -w /dev/null) &&
	echo "$empty_blob" >expect_empty
'

while read cmd
do
	test_expect_success "'git notes $cmd' removes empty note" "
		test_might_fail git notes remove HEAD &&
		MSG= git notes $cmd &&
		test_must_fail git notes list HEAD
	"

	test_expect_success "'git notes $cmd --allow-empty' stores empty note" "
		test_might_fail git notes remove HEAD &&
		MSG= git notes $cmd --allow-empty &&
		git notes list HEAD >actual &&
		test_cmp expect_empty actual
	"
done <<\EOF
add
add -F /dev/null
add -m ""
add -c "$empty_blob"
add -C "$empty_blob"
append
append -F /dev/null
append -m ""
append -c "$empty_blob"
append -C "$empty_blob"
edit
EOF

test_expect_success 'empty notes are displayed by git log' '
	test_cummit 17th &&
	git log -1 >expect &&
	cat >>expect <<-EOF &&

		Notes:
	EOF
	git notes add -C "$empty_blob" --allow-empty &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_done
