#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test commit notes'

. ./test-lib.sh

cat > fake_editor.sh << \EOF
echo "$MSG" > "$1"
echo "$MSG" >& 2
EOF
chmod a+x fake_editor.sh
GIT_EDITOR=./fake_editor.sh
export GIT_EDITOR

test_expect_success 'cannot annotate non-existing HEAD' '
	(MSG=3 && export MSG && test_must_fail git notes edit)
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
	(MSG=1 GIT_NOTES_REF=/ && export MSG GIT_NOTES_REF &&
	 test_must_fail git notes edit) &&
	(MSG=2 GIT_NOTES_REF=/ && export MSG GIT_NOTES_REF &&
	 test_must_fail git notes show)
'

test_expect_success 'refusing to edit in refs/heads/' '
	(MSG=1 GIT_NOTES_REF=refs/heads/bogus &&
	 export MSG GIT_NOTES_REF &&
	 test_must_fail git notes edit)
'

test_expect_success 'refusing to edit in refs/remotes/' '
	(MSG=1 GIT_NOTES_REF=refs/remotes/bogus &&
	 export MSG GIT_NOTES_REF &&
	 test_must_fail git notes edit)
'

# 1 indicates caught gracefully by die, 128 means git-show barked
test_expect_success 'handle empty notes gracefully' '
	git notes show ; test 1 = $?
'

test_expect_success 'create notes' '
	git config core.notesRef refs/notes/commits &&
	MSG=b0 git notes edit &&
	test ! -f .git/NOTES_EDITMSG &&
	test 1 = $(git ls-tree refs/notes/commits | wc -l) &&
	test b0 = $(git notes show) &&
	git show HEAD^ &&
	test_must_fail git notes show HEAD^
'

test_expect_success 'edit existing notes' '
	MSG=b1 git notes edit &&
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
d3d3d3" git notes edit
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
	git notes edit -F note5
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

test_expect_success 'create -m notes (setup)' '
	: > a5 &&
	git add a5 &&
	test_tick &&
	git commit -m 5th &&
	git notes edit -m spam -m "foo
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

test_expect_success 'create other note on a different notes ref (setup)' '
	: > a6 &&
	git add a6 &&
	test_tick &&
	git commit -m 6th &&
	GIT_NOTES_REF="refs/notes/other" git notes edit -m "other note"
'

cat > expect-other << EOF
commit 387a89921c73d7ed72cd94d179c1c7048ca47756
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:18:13 2005 -0700

    6th

Notes:
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

test_expect_success 'Allow notes on non-commits (trees, blobs, tags)' '
	echo "Note on a tree" > expect
	git notes edit -m "Note on a tree" HEAD: &&
	git notes show HEAD: > actual &&
	test_cmp expect actual &&
	echo "Note on a blob" > expect
	filename=$(git ls-tree --name-only HEAD | head -n1) &&
	git notes edit -m "Note on a blob" HEAD:$filename &&
	git notes show HEAD:$filename > actual &&
	test_cmp expect actual &&
	echo "Note on a tag" > expect
	git tag -a -m "This is an annotated tag" foobar HEAD^ &&
	git notes edit -m "Note on a tag" foobar &&
	git notes show foobar > actual &&
	test_cmp expect actual
'

test_done
