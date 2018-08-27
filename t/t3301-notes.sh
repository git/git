#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test commit notes'

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
	test_commit 1st &&
	test_commit 2nd
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
	git ls-tree -r refs/notes/commits >actual &&
	test_line_count = 1 actual &&
	test "b4" = "$(git notes show)" &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'show notes entry with %N' '
	test_write_lines A b4 B >expect &&
	git show -s --format="A%n%NB" >actual &&
	test_cmp expect actual
'

test_expect_success 'create reflog entry' '
	cat <<-EOF >expect &&
		a1d8fa6 refs/notes/commits@{0}: notes: Notes added by '\''git notes add'\''
	EOF
	git reflog show refs/notes/commits >actual &&
	test_cmp expect actual
'

test_expect_success 'edit existing notes' '
	MSG=b3 git notes edit &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/commits >actual &&
	test_line_count = 1 actual &&
	test "b3" = "$(git notes show)" &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'show notes from treeish' '
	test "b3" = "$(git notes --ref commits^{tree} show)" &&
	test "b4" = "$(git notes --ref commits@{1} show)"
'

test_expect_success 'cannot edit notes from non-ref' '
	test_must_fail git notes --ref commits^{tree} edit &&
	test_must_fail git notes --ref commits@{1} edit
'

test_expect_success 'cannot "git notes add -m" where notes already exists' '
	test_must_fail git notes add -m "b2" &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/commits >actual &&
	test_line_count = 1 actual &&
	test "b3" = "$(git notes show)" &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'can overwrite existing note with "git notes add -f -m"' '
	git notes add -f -m "b1" &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/commits >actual &&
	test_line_count = 1 actual &&
	test "b1" = "$(git notes show)" &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'add w/no options on existing note morphs into edit' '
	MSG=b2 git notes add &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/commits >actual &&
	test_line_count = 1 actual &&
	test "b2" = "$(git notes show)" &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'can overwrite existing note with "git notes add -f"' '
	MSG=b1 git notes add -f &&
	test_path_is_missing .git/NOTES_EDITMSG &&
	git ls-tree -r refs/notes/commits >actual &&
	test_line_count = 1 actual &&
	test "b1" = "$(git notes show)" &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'show notes' '
	cat >expect <<-EOF &&
		commit 7a4ca6ee52a974a66cbaa78e33214535dff1d691
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:14:13 2005 -0700

		${indent}2nd

		Notes:
		${indent}b1
	EOF
	! (git cat-file commit HEAD | grep b1) &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'show multi-line notes' '
	test_commit 3rd &&
	MSG="b3${LF}c3c3c3c3${LF}d3d3d3" git notes add &&
	cat >expect-multiline <<-EOF &&
		commit d07d62e5208f22eb5695e7eb47667dc8b9860290
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
	test_commit 4th &&
	echo "xyzzy" >note5 &&
	git notes add -F note5 &&
	cat >expect-F <<-EOF &&
		commit 0f7aa3ec6325aeb88b910453bb3eb37c49d75c11
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
	cat >expect <<-EOF &&
		commit 0f7aa3ec6325aeb88b910453bb3eb37c49d75c11
		tree 05ac65288c4c4b3b709a020ae94b2ece2f2201ae
		parent d07d62e5208f22eb5695e7eb47667dc8b9860290
		author A U Thor <author@example.com> 1112912173 -0700
		committer C O Mitter <committer@example.com> 1112912173 -0700

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
	test_commit 5th &&
	git notes add -m spam -m "foo${LF}bar${LF}baz" &&
	cat >expect-m <<-EOF &&
		commit 7f9ad8836c775acb134c0a055fc55fb4cd1ba361
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
	cat >expect-rm-F <<-EOF &&
		commit 7f9ad8836c775acb134c0a055fc55fb4cd1ba361
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
	cat >expect-rm-remove <<-EOF &&
		commit 7f9ad8836c775acb134c0a055fc55fb4cd1ba361
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:17:13 2005 -0700

		${indent}5th

		commit 0f7aa3ec6325aeb88b910453bb3eb37c49d75c11
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:16:13 2005 -0700

		${indent}4th

	EOF
	cat expect-multiline >>expect-rm-remove &&
	git log -4 >actual &&
	test_cmp expect-rm-remove actual &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'removing non-existing note should not create new commit' '
	git rev-parse --verify refs/notes/commits >before_commit &&
	test_must_fail git notes remove HEAD^ &&
	git rev-parse --verify refs/notes/commits >after_commit &&
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
	cat >expect <<-EOF &&
		c9c6af7f78bc47490dbf3e822cf2f3c24d4b9061 7a4ca6ee52a974a66cbaa78e33214535dff1d691
		c18dc024e14f08d18d14eea0d747ff692d66d6a3 d07d62e5208f22eb5695e7eb47667dc8b9860290
	EOF
	git notes list >actual &&
	test_cmp expect actual
'

test_expect_success 'list notes with "git notes"' '
	git notes >actual &&
	test_cmp expect actual
'

test_expect_success 'list specific note with "git notes list <object>"' '
	cat >expect <<-EOF &&
		c18dc024e14f08d18d14eea0d747ff692d66d6a3
	EOF
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
	cat >expect_list <<-EOF &&
		c9c6af7f78bc47490dbf3e822cf2f3c24d4b9061 7a4ca6ee52a974a66cbaa78e33214535dff1d691
		4b6ad22357cc8a1296720574b8d2fbc22fab0671 7f9ad8836c775acb134c0a055fc55fb4cd1ba361
		c18dc024e14f08d18d14eea0d747ff692d66d6a3 d07d62e5208f22eb5695e7eb47667dc8b9860290
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
	test_commit 6th &&
	GIT_NOTES_REF="refs/notes/other" git notes add -m "other note" &&
	cat >expect-not-other <<-EOF &&
		commit 2c125331118caba0ff8238b7f4958ac6e93fe39c
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
	cat >expect-both <<-EOF &&
		commit 2c125331118caba0ff8238b7f4958ac6e93fe39c
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th

		Notes:
		${indent}order test

		Notes (other):
		${indent}other note

		commit 7f9ad8836c775acb134c0a055fc55fb4cd1ba361
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:17:13 2005 -0700

		${indent}5th

		Notes:
		${indent}replacement for deleted note
	EOF
	GIT_NOTES_REF=refs/notes/commits git notes add \
		-m"replacement for deleted note" HEAD^ &&
	GIT_NOTES_REF=refs/notes/commits git notes add -m"order test" &&
	test_unconfig core.notesRef &&
	test_config notes.displayRef "refs/notes/*" &&
	git log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'core.notesRef is implicitly in notes.displayRef' '
	test_config core.notesRef refs/notes/commits &&
	test_config notes.displayRef refs/notes/other &&
	git log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'notes.displayRef can be given more than once' '
	test_unconfig core.notesRef &&
	test_config notes.displayRef refs/notes/commits &&
	git config --add notes.displayRef refs/notes/other &&
	git log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'notes.displayRef respects order' '
	cat >expect-both-reversed <<-EOF &&
		commit 2c125331118caba0ff8238b7f4958ac6e93fe39c
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th

		Notes (other):
		${indent}other note

		Notes:
		${indent}order test
	EOF
	test_config core.notesRef refs/notes/other &&
	test_config notes.displayRef refs/notes/commits &&
	git log -1 >actual &&
	test_cmp expect-both-reversed actual
'

test_expect_success 'GIT_NOTES_DISPLAY_REF works' '
	GIT_NOTES_DISPLAY_REF=refs/notes/commits:refs/notes/other \
		git log -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success 'GIT_NOTES_DISPLAY_REF overrides config' '
	cat >expect-none <<-EOF &&
		commit 2c125331118caba0ff8238b7f4958ac6e93fe39c
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th

		commit 7f9ad8836c775acb134c0a055fc55fb4cd1ba361
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
	cat >expect-commits <<-EOF &&
		commit 2c125331118caba0ff8238b7f4958ac6e93fe39c
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:18:13 2005 -0700

		${indent}6th

		Notes:
		${indent}order test
	EOF
	git log --no-standard-notes --show-notes=commits -1 >actual &&
	test_cmp expect-commits actual
'

test_expect_success '--standard-notes' '
	test_config notes.displayRef "refs/notes/*" &&
	git log --no-standard-notes --show-notes=commits \
		--standard-notes -2 >actual &&
	test_cmp expect-both actual
'

test_expect_success '--show-notes=ref accumulates' '
	git log --show-notes=other --show-notes=commits \
		 --no-standard-notes -1 >actual &&
	test_cmp expect-both-reversed actual
'

test_expect_success 'Allow notes on non-commits (trees, blobs, tags)' '
	test_config core.notesRef refs/notes/other &&
	echo "Note on a tree" >expect &&
	git notes add -m "Note on a tree" HEAD: &&
	git notes show HEAD: >actual &&
	test_cmp expect actual &&
	echo "Note on a blob" >expect &&
	filename=$(git ls-tree --name-only HEAD | head -n1) &&
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
	cat >expect <<-EOF &&
		commit fb01e0ca8c33b6cc0c6451dde747f97df567cb5c
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:19:13 2005 -0700

		${indent}7th

		Notes:
		${indent}order test
	EOF
	test_commit 7th &&
	git notes add -C $(git notes list HEAD^) &&
	git log -1 >actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD^)"
'

test_expect_success 'create note from non-existing note with "git notes add -C" fails' '
	test_commit 8th &&
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

test_expect_success 'create note from blob with "git notes add -C" reuses blob id' '
	cat >expect <<-EOF &&
		commit 9a4c31c7f722b5d517e92c64e932dd751e1413bf
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:20:13 2005 -0700

		${indent}8th

		Notes:
		${indent}This is a blob object
	EOF
	blob=$(echo "This is a blob object" | git hash-object -w --stdin) &&
	git notes add -C $blob &&
	git log -1 >actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$blob"
'

test_expect_success 'create note from other note with "git notes add -c"' '
	cat >expect <<-EOF &&
		commit 2e0db4bc649e174d667a1cde19e725cf897a5bd2
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:21:13 2005 -0700

		${indent}9th

		Notes:
		${indent}yet another note
	EOF
	test_commit 9th &&
	MSG="yet another note" git notes add -c $(git notes list HEAD^^) &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from non-existing note with "git notes add -c" fails' '
	test_commit 10th &&
	test_must_fail env MSG="yet another note" git notes add -c deadbeef &&
	test_must_fail git notes list HEAD
'

test_expect_success 'append to note from other note with "git notes append -C"' '
	cat >expect <<-EOF &&
		commit 2e0db4bc649e174d667a1cde19e725cf897a5bd2
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:21:13 2005 -0700

		${indent}9th

		Notes:
		${indent}yet another note
		${indent}
		${indent}yet another note
	EOF
	git notes append -C $(git notes list HEAD^) HEAD^ &&
	git log -1 HEAD^ >actual &&
	test_cmp expect actual
'

test_expect_success 'create note from other note with "git notes append -c"' '
	cat >expect <<-EOF &&
		commit 7c3b87ab368f81e11b1ea87b2ab99a71ccd25406
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:22:13 2005 -0700

		${indent}10th

		Notes:
		${indent}other note
	EOF
	MSG="other note" git notes append -c $(git notes list HEAD^) &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'append to note from other note with "git notes append -c"' '
	cat >expect <<-EOF &&
		commit 7c3b87ab368f81e11b1ea87b2ab99a71ccd25406
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:22:13 2005 -0700

		${indent}10th

		Notes:
		${indent}other note
		${indent}
		${indent}yet another note
	EOF
	MSG="yet another note" git notes append -c $(git notes list HEAD) &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'copy note with "git notes copy"' '
	cat >expect <<-EOF &&
		commit a446fff8777efdc6eb8f4b7c8a5ff699484df0d5
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:23:13 2005 -0700

		${indent}11th

		Notes:
		${indent}other note
		${indent}
		${indent}yet another note
	EOF
	test_commit 11th &&
	git notes copy HEAD^ HEAD &&
	git log -1 >actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD^)"
'

test_expect_success 'prevent overwrite with "git notes copy"' '
	test_must_fail git notes copy HEAD~2 HEAD &&
	git log -1 >actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD^)"
'

test_expect_success 'allow overwrite with "git notes copy -f"' '
	cat >expect <<-EOF &&
		commit a446fff8777efdc6eb8f4b7c8a5ff699484df0d5
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:23:13 2005 -0700

		${indent}11th

		Notes:
		${indent}yet another note
		${indent}
		${indent}yet another note
	EOF
	git notes copy -f HEAD~2 HEAD &&
	git log -1 >actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD~2)"
'

test_expect_success 'cannot copy note from object without notes' '
	test_commit 12th &&
	test_commit 13th &&
	test_must_fail git notes copy HEAD^ HEAD
'

test_expect_success 'git notes copy --stdin' '
	cat >expect <<-EOF &&
		commit e871aa61182b1d95d0a6fb75445d891722863b6b
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:25:13 2005 -0700

		${indent}13th

		Notes:
		${indent}yet another note
		${indent}
		${indent}yet another note

		commit 65e263ded02ae4e8839bc151095113737579dc12
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:24:13 2005 -0700

		${indent}12th

		Notes:
		${indent}other note
		${indent}
		${indent}yet another note
	EOF
	(echo $(git rev-parse HEAD~3) $(git rev-parse HEAD^) &&
	echo $(git rev-parse HEAD~2) $(git rev-parse HEAD)) |
	git notes copy --stdin &&
	git log -2 >actual &&
	test_cmp expect actual &&
	test "$(git notes list HEAD)" = "$(git notes list HEAD~2)" &&
	test "$(git notes list HEAD^)" = "$(git notes list HEAD~3)"
'

test_expect_success 'git notes copy --for-rewrite (unconfigured)' '
	cat >expect <<-EOF &&
		commit 4acf42e847e7fffbbf89ee365c20ac7caf40de89
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		commit 07c85d77059393ed0154b8c96906547a59dfcddd
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:26:13 2005 -0700

		${indent}14th
	EOF
	test_commit 14th &&
	test_commit 15th &&
	(echo $(git rev-parse HEAD~3) $(git rev-parse HEAD^) &&
	echo $(git rev-parse HEAD~2) $(git rev-parse HEAD)) |
	git notes copy --for-rewrite=foo &&
	git log -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (enabled)' '
	cat >expect <<-EOF &&
		commit 4acf42e847e7fffbbf89ee365c20ac7caf40de89
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}yet another note
		${indent}
		${indent}yet another note

		commit 07c85d77059393ed0154b8c96906547a59dfcddd
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
	(echo $(git rev-parse HEAD~3) $(git rev-parse HEAD^) &&
	echo $(git rev-parse HEAD~2) $(git rev-parse HEAD)) |
	git notes copy --for-rewrite=foo &&
	git log -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (disabled)' '
	test_config notes.rewrite.bar false &&
	echo $(git rev-parse HEAD~3) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=bar &&
	git log -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (overwrite)' '
	cat >expect <<-EOF &&
		commit 4acf42e847e7fffbbf89ee365c20ac7caf40de89
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}a fresh note
	EOF
	git notes add -f -m"a fresh note" HEAD^ &&
	test_config notes.rewriteMode overwrite &&
	test_config notes.rewriteRef "refs/notes/*" &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=foo &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (ignore)' '
	test_config notes.rewriteMode ignore &&
	test_config notes.rewriteRef "refs/notes/*" &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=foo &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (append)' '
	cat >expect <<-EOF &&
		commit 4acf42e847e7fffbbf89ee365c20ac7caf40de89
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
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=foo &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (append two to one)' '
	cat >expect <<-EOF &&
		commit 4acf42e847e7fffbbf89ee365c20ac7caf40de89
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
	(echo $(git rev-parse HEAD^) $(git rev-parse HEAD) &&
	echo $(git rev-parse HEAD^^) $(git rev-parse HEAD)) |
	git notes copy --for-rewrite=foo &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy --for-rewrite (append empty)' '
	git notes remove HEAD^ &&
	test_config notes.rewriteMode concatenate &&
	test_config notes.rewriteRef "refs/notes/*" &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	git notes copy --for-rewrite=foo &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_NOTES_REWRITE_MODE works' '
	cat >expect <<-EOF &&
		commit 4acf42e847e7fffbbf89ee365c20ac7caf40de89
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}replacement note 1
	EOF
	test_config notes.rewriteMode concatenate &&
	test_config notes.rewriteRef "refs/notes/*" &&
	git notes add -f -m"replacement note 1" HEAD^ &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	GIT_NOTES_REWRITE_MODE=overwrite git notes copy --for-rewrite=foo &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_NOTES_REWRITE_REF works' '
	cat >expect <<-EOF &&
		commit 4acf42e847e7fffbbf89ee365c20ac7caf40de89
		Author: A U Thor <author@example.com>
		Date:   Thu Apr 7 15:27:13 2005 -0700

		${indent}15th

		Notes:
		${indent}replacement note 2
	EOF
	git notes add -f -m"replacement note 2" HEAD^ &&
	test_config notes.rewriteMode overwrite &&
	test_unconfig notes.rewriteRef &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	GIT_NOTES_REWRITE_REF=refs/notes/commits:refs/notes/other \
		git notes copy --for-rewrite=foo &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_NOTES_REWRITE_REF overrides config' '
	git notes add -f -m"replacement note 3" HEAD^ &&
	test_config notes.rewriteMode overwrite &&
	test_config notes.rewriteRef refs/notes/other &&
	echo $(git rev-parse HEAD^) $(git rev-parse HEAD) |
	GIT_NOTES_REWRITE_REF= git notes copy --for-rewrite=foo &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'git notes copy diagnoses too many or too few parameters' '
	test_must_fail git notes copy &&
	test_must_fail git notes copy one two three
'

test_expect_success 'git notes get-ref expands refs/heads/master to refs/notes/refs/heads/master' '
	test_unconfig core.notesRef &&
	sane_unset GIT_NOTES_REF &&
	test "$(git notes --ref=refs/heads/master get-ref)" = "refs/notes/refs/heads/master"
'

test_expect_success 'git notes get-ref (no overrides)' '
	test_unconfig core.notesRef &&
	sane_unset GIT_NOTES_REF &&
	test "$(git notes get-ref)" = "refs/notes/commits"
'

test_expect_success 'git notes get-ref (core.notesRef)' '
	test_config core.notesRef refs/notes/foo &&
	test "$(git notes get-ref)" = "refs/notes/foo"
'

test_expect_success 'git notes get-ref (GIT_NOTES_REF)' '
	test "$(GIT_NOTES_REF=refs/notes/bar git notes get-ref)" = "refs/notes/bar"
'

test_expect_success 'git notes get-ref (--ref)' '
	test "$(GIT_NOTES_REF=refs/notes/bar git notes --ref=baz get-ref)" = "refs/notes/baz"
'

test_expect_success 'setup testing of empty notes' '
	test_unconfig core.notesRef &&
	test_commit 16th &&
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
	test_commit 17th &&
	git log -1 >expect &&
	cat >>expect <<-EOF &&

		Notes:
	EOF
	git notes add -C "$empty_blob" --allow-empty &&
	git log -1 >actual &&
	test_cmp expect actual
'

test_done
