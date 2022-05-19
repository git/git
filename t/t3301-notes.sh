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
	test_must_fail env MSG=3 but notes add
'

test_expect_success 'setup' '
	test_cummit 1st &&
	test_cummit 2nd
'

test_expect_success 'need valid notes ref' '
	test_must_fail env MSG=1 GIT_NOTES_REF=/ but notes show &&
	test_must_fail env MSG=2 GIT_NOTES_REF=/ but notes show
'

test_expect_success 'refusing to add notes in refs/heads/' '
	test_must_fail env MSG=1 GIT_NOTES_REF=refs/heads/bogus but notes add
'

test_expect_success 'refusing to edit notes in refs/remotes/' '
	test_must_fail env MSG=1 GIT_NOTES_REF=refs/heads/bogus but notes edit
'

# 1 indicates caught gracefully by die, 128 means but-show barked
test_expect_success 'handle empty notes gracefully' '
	test_expect_code 1 but notes show
'

test_expect_success 'show non-existent notes entry with %N' '
	test_write_lines A B >expect &&
	but show -s --format="A%n%NB" >actual &&
	test_cmp expect actual
'

test_expect_success 'create notes' '
	MSG=b4 but notes add &&
	test_path_is_missing .but/NOTES_EDITMSG &&
	but ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b4 >expect &&
	but notes show >actual &&
	test_cmp expect actual &&
	but show HEAD^ &&
	test_must_fail but notes show HEAD^
'

test_expect_success 'show notes entry with %N' '
	test_write_lines A b4 B >expect &&
	but show -s --format="A%n%NB" >actual &&
	test_cmp expect actual
'

test_expect_success 'create reflog entry' '
	ref=$(but rev-parse --short refs/notes/cummits) &&
	cat <<-EOF >expect &&
		$ref refs/notes/cummits@{0}: notes: Notes added by '\''but notes add'\''
	EOF
	but reflog show refs/notes/cummits >actual &&
	test_cmp expect actual
'

test_expect_success 'edit existing notes' '
	MSG=b3 but notes edit &&
	test_path_is_missing .but/NOTES_EDITMSG &&
	but ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b3 >expect &&
	but notes show >actual &&
	test_cmp expect actual &&
	but show HEAD^ &&
	test_must_fail but notes show HEAD^
'

test_expect_success 'show notes from treeish' '
	echo b3 >expect &&
	but notes --ref cummits^{tree} show >actual &&
	test_cmp expect actual &&

	echo b4 >expect &&
	but notes --ref cummits@{1} show >actual &&
	test_cmp expect actual
'

test_expect_success 'cannot edit notes from non-ref' '
	test_must_fail but notes --ref cummits^{tree} edit &&
	test_must_fail but notes --ref cummits@{1} edit
'

test_expect_success 'cannot "but notes add -m" where notes already exists' '
	test_must_fail but notes add -m "b2" &&
	test_path_is_missing .but/NOTES_EDITMSG &&
	but ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b3 >expect &&
	but notes show >actual &&
	test_cmp expect actual &&
	but show HEAD^ &&
	test_must_fail but notes show HEAD^
'

test_expect_success 'can overwrite existing note with "but notes add -f -m"' '
	but notes add -f -m "b1" &&
	test_path_is_missing .but/NOTES_EDITMSG &&
	but ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b1 >expect &&
	but notes show >actual &&
	test_cmp expect actual &&
	but show HEAD^ &&
	test_must_fail but notes show HEAD^
'

test_expect_success 'add w/no options on existing note morphs into edit' '
	MSG=b2 but notes add &&
	test_path_is_missing .but/NOTES_EDITMSG &&
	but ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b2 >expect &&
	but notes show >actual &&
	test_cmp expect actual &&
	but show HEAD^ &&
	test_must_fail but notes show HEAD^
'

test_expect_success 'can overwrite existing note with "but notes add -f"' '
	MSG=b1 but notes add -f &&
	test_path_is_missing .but/NOTES_EDITMSG &&
	but ls-tree -r refs/notes/cummits >actual &&
	test_line_count = 1 actual &&
	echo b1 >expect &&
	but notes show >actual &&
	test_cmp expect actual &&
	but show HEAD^ &&
	test_must_fail but notes show HEAD^
'

test_expect_success 'show notes' '
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:14:13 2005 -0700

		${indent}2nd

		Notes:
		${indent}b1
	EOF
	but cat-file commit HEAD >cummits &&
	! grep b1 cummits &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'show multi-line notes' '
	test_cummit 3rd &&
	MSG="b3${LF}c3c3c3c3${LF}d3d3d3" but notes add &&
	cummit=$(but rev-parse HEAD) &&
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
	but log -2 >actual &&
	test_cmp expect-multiline actual
'

test_expect_success 'show -F notes' '
	test_cummit 4th &&
	echo "xyzzy" >note5 &&
	but notes add -F note5 &&
	cummit=$(but rev-parse HEAD) &&
	cat >expect-F <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:16:13 2005 -0700

		${indent}4th

		Notes:
		${indent}xyzzy

	EOF
	cat expect-multiline >>expect-F &&
	but log -3 >actual &&
	test_cmp expect-F actual
'

test_expect_success 'Re-adding -F notes without -f fails' '
	echo "zyxxy" >note5 &&
	test_must_fail but notes add -F note5 &&
	but log -3 >actual &&
	test_cmp expect-F actual
'

test_expect_success 'but log --pretty=raw does not show notes' '
	cummit=$(but rev-parse HEAD) &&
	tree=$(but rev-parse HEAD^{tree}) &&
	parent=$(but rev-parse HEAD^) &&
	cat >expect <<-EOF &&
		cummit $cummit
		tree $tree
		parent $parent
		author A U Thor <author@example.com> 1112912173 -0700
		cummitter C O Mitter <cummitter@example.com> 1112912173 -0700

		${indent}4th
	EOF
	but log -1 --pretty=raw >actual &&
	test_cmp expect actual
'

test_expect_success 'but log --show-notes' '
	cat >>expect <<-EOF &&

	Notes:
	${indent}xyzzy
	EOF
	but log -1 --pretty=raw --show-notes >actual &&
	test_cmp expect actual
'

test_expect_success 'but log --no-notes' '
	but log -1 --no-notes >actual &&
	! grep xyzzy actual
'

test_expect_success 'but format-patch does not show notes' '
	but format-patch -1 --stdout >actual &&
	! grep xyzzy actual
'

test_expect_success 'but format-patch --show-notes does show notes' '
	but format-patch --show-notes -1 --stdout >actual &&
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
	test_expect_success "but show $pretty does$not show notes" '
		but show $p >actual &&
		eval "$negate grep xyzzy actual"
	'
done

test_expect_success 'setup alternate notes ref' '
	but notes --ref=alternate add -m alternate
'

test_expect_success 'but log --notes shows default notes' '
	but log -1 --notes >actual &&
	grep xyzzy actual &&
	! grep alternate actual
'

test_expect_success 'but log --notes=X shows only X' '
	but log -1 --notes=alternate >actual &&
	! grep xyzzy actual &&
	grep alternate actual
'

test_expect_success 'but log --notes --notes=X shows both' '
	but log -1 --notes --notes=alternate >actual &&
	grep xyzzy actual &&
	grep alternate actual
'

test_expect_success 'but log --no-notes resets default state' '
	but log -1 --notes --notes=alternate \
		--no-notes --notes=alternate \
		>actual &&
	! grep xyzzy actual &&
	grep alternate actual
'

test_expect_success 'but log --no-notes resets ref list' '
	but log -1 --notes --notes=alternate \
		--no-notes --notes \
		>actual &&
	grep xyzzy actual &&
	! grep alternate actual
'

test_expect_success 'show -m notes' '
	test_cummit 5th &&
	but notes add -m spam -m "foo${LF}bar${LF}baz" &&
	cummit=$(but rev-parse HEAD) &&
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
	but log -4 >actual &&
	test_cmp expect-m actual
'

test_expect_success 'remove note with add -f -F /dev/null' '
	but notes add -f -F /dev/null &&
	cummit=$(but rev-parse HEAD) &&
	cat >expect-rm-F <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:17:13 2005 -0700

		${indent}5th

	EOF
	cat expect-F >>expect-rm-F &&
	but log -4 >actual &&
	test_cmp expect-rm-F actual &&
	test_must_fail but notes show
'

test_expect_success 'do not create empty note with -m ""' '
	but notes add -m "" &&
	but log -4 >actual &&
	test_cmp expect-rm-F actual &&
	test_must_fail but notes show
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
	but notes add -m "foo" -F note_a -m "bar" -F note_b -m "baz" &&
	but notes show >actual &&
	test_cmp expect-combine_m_and_F actual
'

test_expect_success 'remove note with "but notes remove"' '
	but notes remove HEAD^ &&
	but notes remove &&
	cummit=$(but rev-parse HEAD) &&
	parent=$(but rev-parse HEAD^) &&
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
	but log -4 >actual &&
	test_cmp expect-rm-remove actual &&
	test_must_fail but notes show HEAD^
'

test_expect_success 'removing non-existing note should not create new cummit' '
	but rev-parse --verify refs/notes/cummits >before_cummit &&
	test_must_fail but notes remove HEAD^ &&
	but rev-parse --verify refs/notes/cummits >after_cummit &&
	test_cmp before_cummit after_cummit
'

test_expect_success 'removing more than one' '
	before=$(but rev-parse --verify refs/notes/cummits) &&
	test_when_finished "but update-ref refs/notes/cummits $before" &&

	# We have only two -- add another and make sure it stays
	but notes add -m "extra" &&
	but notes list HEAD >after-removal-expect &&
	but notes remove HEAD^^ HEAD^^^ &&
	but notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'removing is atomic' '
	before=$(but rev-parse --verify refs/notes/cummits) &&
	test_when_finished "but update-ref refs/notes/cummits $before" &&
	test_must_fail but notes remove HEAD^^ HEAD^^^ HEAD^ &&
	after=$(but rev-parse --verify refs/notes/cummits) &&
	test "$before" = "$after"
'

test_expect_success 'removing with --ignore-missing' '
	before=$(but rev-parse --verify refs/notes/cummits) &&
	test_when_finished "but update-ref refs/notes/cummits $before" &&

	# We have only two -- add another and make sure it stays
	but notes add -m "extra" &&
	but notes list HEAD >after-removal-expect &&
	but notes remove --ignore-missing HEAD^^ HEAD^^^ HEAD^ &&
	but notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'removing with --ignore-missing but bogus ref' '
	before=$(but rev-parse --verify refs/notes/cummits) &&
	test_when_finished "but update-ref refs/notes/cummits $before" &&
	test_must_fail but notes remove --ignore-missing HEAD^^ HEAD^^^ NO-SUCH-cummit &&
	after=$(but rev-parse --verify refs/notes/cummits) &&
	test "$before" = "$after"
'

test_expect_success 'remove reads from --stdin' '
	before=$(but rev-parse --verify refs/notes/cummits) &&
	test_when_finished "but update-ref refs/notes/cummits $before" &&

	# We have only two -- add another and make sure it stays
	but notes add -m "extra" &&
	but notes list HEAD >after-removal-expect &&
	but rev-parse HEAD^^ HEAD^^^ >input &&
	but notes remove --stdin <input &&
	but notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'remove --stdin is also atomic' '
	before=$(but rev-parse --verify refs/notes/cummits) &&
	test_when_finished "but update-ref refs/notes/cummits $before" &&
	but rev-parse HEAD^^ HEAD^^^ HEAD^ >input &&
	test_must_fail but notes remove --stdin <input &&
	after=$(but rev-parse --verify refs/notes/cummits) &&
	test "$before" = "$after"
'

test_expect_success 'removing with --stdin --ignore-missing' '
	before=$(but rev-parse --verify refs/notes/cummits) &&
	test_when_finished "but update-ref refs/notes/cummits $before" &&

	# We have only two -- add another and make sure it stays
	but notes add -m "extra" &&
	but notes list HEAD >after-removal-expect &&
	but rev-parse HEAD^^ HEAD^^^ HEAD^ >input &&
	but notes remove --ignore-missing --stdin <input &&
	but notes list | sed -e "s/ .*//" >actual &&
	test_cmp after-removal-expect actual
'

test_expect_success 'list notes with "but notes list"' '
	cummit_2=$(but rev-parse 2nd) &&
	cummit_3=$(but rev-parse 3rd) &&
	note_2=$(but rev-parse refs/notes/cummits:$cummit_2) &&
	note_3=$(but rev-parse refs/notes/cummits:$cummit_3) &&
	sort -t" " -k2 >expect <<-EOF &&
		$note_2 $cummit_2
		$note_3 $cummit_3
	EOF
	but notes list >actual &&
	test_cmp expect actual
'

test_expect_success 'list notes with "but notes"' '
	but notes >actual &&
	test_cmp expect actual
'

test_expect_success 'list specific note with "but notes list <object>"' '
	but rev-parse refs/notes/cummits:$cummit_3 >expect &&
	but notes list HEAD^^ >actual &&
	test_cmp expect actual
'

test_expect_success 'listing non-existing notes fails' '
	test_must_fail but notes list HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'append to existing note with "but notes append"' '
	cat >expect <<-EOF &&
		Initial set of notes

		More notes appended with but notes append
	EOF
	but notes add -m "Initial set of notes" &&
	but notes append -m "More notes appended with but notes append" &&
	but notes show >actual &&
	test_cmp expect actual
'

test_expect_success '"but notes list" does not expand to "but notes list HEAD"' '
	cummit_5=$(but rev-parse 5th) &&
	note_5=$(but rev-parse refs/notes/cummits:$cummit_5) &&
	sort -t" " -k2 >expect_list <<-EOF &&
		$note_2 $cummit_2
		$note_3 $cummit_3
		$note_5 $cummit_5
	EOF
	but notes list >actual &&
	test_cmp expect_list actual
'

test_expect_success 'appending empty string does not change existing note' '
	but notes append -m "" &&
	but notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes append == add when there is no existing note' '
	but notes remove HEAD &&
	test_must_fail but notes list HEAD &&
	but notes append -m "Initial set of notes${LF}${LF}More notes appended with but notes append" &&
	but notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'appending empty string to non-existing note does not create note' '
	but notes remove HEAD &&
	test_must_fail but notes list HEAD &&
	but notes append -m "" &&
	test_must_fail but notes list HEAD
'

test_expect_success 'create other note on a different notes ref (setup)' '
	test_cummit 6th &&
	GIT_NOTES_REF="refs/notes/other" but notes add -m "other note" &&
	cummit=$(but rev-parse HEAD) &&
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
	but log -1 >actual &&
	test_cmp expect-not-other actual
'

test_expect_success 'Do show note when ref is given in GIT_NOTES_REF' '
	GIT_NOTES_REF="refs/notes/other" but log -1 >actual &&
	test_cmp expect-other actual
'

test_expect_success 'Do show note when ref is given in core.notesRef config' '
	test_config core.notesRef "refs/notes/other" &&
	but log -1 >actual &&
	test_cmp expect-other actual
'

test_expect_success 'Do not show note when core.notesRef is overridden' '
	test_config core.notesRef "refs/notes/other" &&
	GIT_NOTES_REF="refs/notes/wrong" but log -1 >actual &&
	test_cmp expect-not-other actual
'

test_expect_success 'Show all notes when notes.displayRef=refs/notes/*' '
	cummit=$(but rev-parse HEAD) &&
	parent=$(but rev-parse HEAD^) &&
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
	GIT_NOTES_REF=refs/notes/cummits but notes add \
		-m"replacement for deleted note" HEAD^ &&
	GIT_NOTES_REF=refs/notes/cummits but notes add -m"order test" &&
	test_unconfig core.notesRef &&
	test_config notes.displayRef "refs/notes/*" &&
	but log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'core.notesRef is implicitly in notes.displayRef' '
	test_config core.notesRef refs/notes/cummits &&
	test_config notes.displayRef refs/notes/other &&
	but log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'notes.displayRef can be given more than once' '
	test_unconfig core.notesRef &&
	test_config notes.displayRef refs/notes/cummits &&
	but config --add notes.displayRef refs/notes/other &&
	but log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'notes.displayRef respects order' '
	cummit=$(but rev-parse HEAD) &&
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
	but log -1 >actual &&
	test_cmp expect-both-reversed actual
'

test_expect_success 'notes.displayRef with no value handled gracefully' '
	test_must_fail but -c notes.displayRef log -0 --notes &&
	test_must_fail but -c notes.displayRef diff-tree --notes HEAD
'

test_expect_success 'GIT_NOTES_DISPLAY_REF works' '
	GIT_NOTES_DISPLAY_REF=refs/notes/cummits:refs/notes/other \
		but log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'GIT_NOTES_DISPLAY_REF overrides config' '
	cummit=$(but rev-parse HEAD) &&
	parent=$(but rev-parse HEAD^) &&
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
	GIT_NOTES_REF= GIT_NOTES_DISPLAY_REF= but log -2 >actual &&
	test_cmp expect-none actual
'

test_expect_success '--show-notes=* adds to GIT_NOTES_DISPLAY_REF' '
	GIT_NOTES_REF= GIT_NOTES_DISPLAY_REF= but log --show-notes=* -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success '--no-standard-notes' '
	cummit=$(but rev-parse HEAD) &&
	cat >expect-cummits <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th

		Notes:
		${indent}order test
	EOF
	but log --no-standard-notes --show-notes=cummits -1 >actual &&
	test_cmp expect-cummits actual
'

test_expect_success '--standard-notes' '
	test_config notes.displayRef "refs/notes/*" &&
	but log --no-standard-notes --show-notes=cummits \
		--standard-notes -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success '--show-notes=ref accumulates' '
	but log --show-notes=other --show-notes=cummits \
		 --no-standard-notes -1 >actual &&
	test_cmp expect-both-reversed actual
'

test_expect_success 'Allow notes on non-cummits (trees, blobs, tags)' '
	test_config core.notesRef refs/notes/other &&
	echo "Note on a tree" >expect &&
	but notes add -m "Note on a tree" HEAD: &&
	but notes show HEAD: >actual &&
	test_cmp expect actual &&
	echo "Note on a blob" >expect &&
	but ls-tree --name-only HEAD >files &&
	filename=$(head -n1 files) &&
	but notes add -m "Note on a blob" HEAD:$filename &&
	but notes show HEAD:$filename >actual &&
	test_cmp expect actual &&
	echo "Note on a tag" >expect &&
	but tag -a -m "This is an annotated tag" foobar HEAD^ &&
	but notes add -m "Note on a tag" foobar &&
	but notes show foobar >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from other note with "but notes add -C"' '
	test_cummit 7th &&
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:19:13 2005 -0700

		${indent}7th

		Notes:
		${indent}order test
	EOF
	note=$(but notes list HEAD^) &&
	but notes add -C $note &&
	but log -1 >actual &&
	test_cmp expect actual &&
	but notes list HEAD^ >expect &&
	but notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from non-existing note with "but notes add -C" fails' '
	test_cummit 8th &&
	test_must_fail but notes add -C deadbeef &&
	test_must_fail but notes list HEAD
'

test_expect_success 'create note from non-blob with "but notes add -C" fails' '
	cummit=$(but rev-parse --verify HEAD) &&
	tree=$(but rev-parse --verify HEAD:) &&
	test_must_fail but notes add -C $cummit &&
	test_must_fail but notes add -C $tree &&
	test_must_fail but notes list HEAD
'

test_expect_success 'create note from blob with "but notes add -C" reuses blob id' '
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:20:13 2005 -0700

		${indent}8th

		Notes:
		${indent}This is a blob object
	EOF
	echo "This is a blob object" | but hash-object -w --stdin >blob &&
	but notes add -C $(cat blob) &&
	but log -1 >actual &&
	test_cmp expect actual &&
	but notes list HEAD >actual &&
	test_cmp blob actual
'

test_expect_success 'create note from other note with "but notes add -c"' '
	test_cummit 9th &&
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:21:13 2005 -0700

		${indent}9th

		Notes:
		${indent}yet another note
	EOF
	note=$(but notes list HEAD^^) &&
	MSG="yet another note" but notes add -c $note &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from non-existing note with "but notes add -c" fails' '
	test_cummit 10th &&
	test_must_fail env MSG="yet another note" but notes add -c deadbeef &&
	test_must_fail but notes list HEAD
'

test_expect_success 'append to note from other note with "but notes append -C"' '
	cummit=$(but rev-parse HEAD^) &&
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
	note=$(but notes list HEAD^) &&
	but notes append -C $note HEAD^ &&
	but log -1 HEAD^ >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from other note with "but notes append -c"' '
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:22:13 2005 -0700

		${indent}10th

		Notes:
		${indent}other note
	EOF
	note=$(but notes list HEAD^) &&
	MSG="other note" but notes append -c $note &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'append to note from other note with "but notes append -c"' '
	cummit=$(but rev-parse HEAD) &&
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
	note=$(but notes list HEAD) &&
	MSG="yet another note" but notes append -c $note &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'copy note with "but notes copy"' '
	cummit=$(but rev-parse 4th) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:16:13 2005 -0700

		${indent}4th

		Notes:
		${indent}This is a blob object
	EOF
	but notes copy 8th 4th &&
	but log 3rd..4th >actual &&
	test_cmp expect actual &&
	but notes list 4th >expect &&
	but notes list 8th >actual &&
	test_cmp expect actual
'

test_expect_success 'copy note with "but notes copy" with default' '
	test_cummit 11th &&
	cummit=$(but rev-parse HEAD) &&
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
	but notes copy HEAD^ &&
	but log -1 >actual &&
	test_cmp expect actual &&
	but notes list HEAD^ >expect &&
	but notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'prevent overwrite with "but notes copy"' '
	test_must_fail but notes copy HEAD~2 HEAD &&
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
	but log -1 >actual &&
	test_cmp expect actual &&
	but notes list HEAD^ >expect &&
	but notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'allow overwrite with "but notes copy -f"' '
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:23:13 2005 -0700

		${indent}11th

		Notes:
		${indent}This is a blob object
	EOF
	but notes copy -f HEAD~3 HEAD &&
	but log -1 >actual &&
	test_cmp expect actual &&
	but notes list HEAD~3 >expect &&
	but notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'allow overwrite with "but notes copy -f" with default' '
	cummit=$(but rev-parse HEAD) &&
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
	but notes copy -f HEAD~2 &&
	but log -1 >actual &&
	test_cmp expect actual &&
	but notes list HEAD~2 >expect &&
	but notes list HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'cannot copy note from object without notes' '
	test_cummit 12th &&
	test_cummit 13th &&
	test_must_fail but notes copy HEAD^ HEAD
'

test_expect_success 'but notes copy --stdin' '
	cummit=$(but rev-parse HEAD) &&
	parent=$(but rev-parse HEAD^) &&
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
	from=$(but rev-parse HEAD~3) &&
	to=$(but rev-parse HEAD^) &&
	echo "$from" "$to" >copy &&
	from=$(but rev-parse HEAD~2) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >>copy &&
	but notes copy --stdin <copy &&
	but log -2 >actual &&
	test_cmp expect actual &&
	but notes list HEAD~2 >expect &&
	but notes list HEAD >actual &&
	test_cmp expect actual &&
	but notes list HEAD~3 >expect &&
	but notes list HEAD^ >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes copy --for-rewrite (unconfigured)' '
	test_cummit 14th &&
	test_cummit 15th &&
	cummit=$(but rev-parse HEAD) &&
	parent=$(but rev-parse HEAD^) &&
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
	from=$(but rev-parse HEAD~3) &&
	to=$(but rev-parse HEAD^) &&
	echo "$from" "$to" >copy &&
	from=$(but rev-parse HEAD~2) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >>copy &&
	but notes copy --for-rewrite=foo <copy &&
	but log -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes copy --for-rewrite (enabled)' '
	cummit=$(but rev-parse HEAD) &&
	parent=$(but rev-parse HEAD^) &&
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
	from=$(but rev-parse HEAD~3) &&
	to=$(but rev-parse HEAD^) &&
	echo "$from" "$to" >copy &&
	from=$(but rev-parse HEAD~2) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >>copy &&
	but notes copy --for-rewrite=foo <copy &&
	but log -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes copy --for-rewrite (disabled)' '
	test_config notes.rewrite.bar false &&
	from=$(but rev-parse HEAD~3) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	but notes copy --for-rewrite=bar <copy &&
	but log -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes copy --for-rewrite (overwrite)' '
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}a fresh note
	EOF
	but notes add -f -m"a fresh note" HEAD^ &&
	test_config notes.rewriteMode overwrite &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(but rev-parse HEAD^) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	but notes copy --for-rewrite=foo <copy &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes copy --for-rewrite (ignore)' '
	test_config notes.rewriteMode ignore &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(but rev-parse HEAD^) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	but notes copy --for-rewrite=foo <copy &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes copy --for-rewrite (append)' '
	cummit=$(but rev-parse HEAD) &&
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
	but notes add -f -m"another fresh note" HEAD^ &&
	test_config notes.rewriteMode concatenate &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(but rev-parse HEAD^) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	but notes copy --for-rewrite=foo <copy &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes copy --for-rewrite (append two to one)' '
	cummit=$(but rev-parse HEAD) &&
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
	but notes add -f -m"append 1" HEAD^ &&
	but notes add -f -m"append 2" HEAD^^ &&
	test_config notes.rewriteMode concatenate &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(but rev-parse HEAD^) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	from=$(but rev-parse HEAD^^) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >>copy &&
	but notes copy --for-rewrite=foo <copy &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes copy --for-rewrite (append empty)' '
	but notes remove HEAD^ &&
	test_config notes.rewriteMode concatenate &&
	test_config notes.rewriteRef "refs/notes/*" &&
	from=$(but rev-parse HEAD^) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	but notes copy --for-rewrite=foo <copy &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_NOTES_REWRITE_MODE works' '
	cummit=$(but rev-parse HEAD) &&
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
	but notes add -f -m"replacement note 1" HEAD^ &&
	from=$(but rev-parse HEAD^) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	GIT_NOTES_REWRITE_MODE=overwrite but notes copy --for-rewrite=foo <copy &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_NOTES_REWRITE_REF works' '
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
		cummit $cummit
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}replacement note 2
	EOF
	but notes add -f -m"replacement note 2" HEAD^ &&
	test_config notes.rewriteMode overwrite &&
	test_unconfig notes.rewriteRef &&
	from=$(but rev-parse HEAD^) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	GIT_NOTES_REWRITE_REF=refs/notes/cummits:refs/notes/other \
		but notes copy --for-rewrite=foo <copy &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_NOTES_REWRITE_REF overrides config' '
	but notes add -f -m"replacement note 3" HEAD^ &&
	test_config notes.rewriteMode overwrite &&
	test_config notes.rewriteRef refs/notes/other &&
	from=$(but rev-parse HEAD^) &&
	to=$(but rev-parse HEAD) &&
	echo "$from" "$to" >copy &&
	GIT_NOTES_REWRITE_REF=refs/notes/cummits \
		but notes copy --for-rewrite=foo <copy &&
	but log -1 >actual &&
	grep "replacement note 3" actual
'

test_expect_success 'but notes copy diagnoses too many or too few arguments' '
	test_must_fail but notes copy 2>error &&
	test_i18ngrep "too few arguments" error &&
	test_must_fail but notes copy one two three 2>error &&
	test_i18ngrep "too many arguments" error
'

test_expect_success 'but notes get-ref expands refs/heads/main to refs/notes/refs/heads/main' '
	test_unconfig core.notesRef &&
	sane_unset GIT_NOTES_REF &&
	echo refs/notes/refs/heads/main >expect &&
	but notes --ref=refs/heads/main get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes get-ref (no overrides)' '
	test_unconfig core.notesRef &&
	sane_unset GIT_NOTES_REF &&
	echo refs/notes/cummits >expect &&
	but notes get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes get-ref (core.notesRef)' '
	test_config core.notesRef refs/notes/foo &&
	echo refs/notes/foo >expect &&
	but notes get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes get-ref (GIT_NOTES_REF)' '
	echo refs/notes/bar >expect &&
	GIT_NOTES_REF=refs/notes/bar but notes get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'but notes get-ref (--ref)' '
	echo refs/notes/baz >expect &&
	GIT_NOTES_REF=refs/notes/bar but notes --ref=baz get-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'setup testing of empty notes' '
	test_unconfig core.notesRef &&
	test_cummit 16th &&
	empty_blob=$(but hash-object -w /dev/null) &&
	echo "$empty_blob" >expect_empty
'

while read cmd
do
	test_expect_success "'but notes $cmd' removes empty note" "
		test_might_fail but notes remove HEAD &&
		MSG= but notes $cmd &&
		test_must_fail but notes list HEAD
	"

	test_expect_success "'but notes $cmd --allow-empty' stores empty note" "
		test_might_fail but notes remove HEAD &&
		MSG= but notes $cmd --allow-empty &&
		but notes list HEAD >actual &&
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

test_expect_success 'empty notes are displayed by but log' '
	test_cummit 17th &&
	but log -1 >expect &&
	cat >>expect <<-EOF &&

		Notes:
	EOF
	but notes add -C "$empty_blob" --allow-empty &&
	but log -1 >actual &&
	test_cmp expect actual
'

test_done
