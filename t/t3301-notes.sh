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
VISUAL=./fake_editor.sh
export VISUAL

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
	MSG=b1 git notes edit &&
	test ! -f .git/new-notes &&
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
test_expect_success 'create -m and -F notes (setup)' '
	: > a4 &&
	git add a4 &&
	test_tick &&
	git commit -m 4th &&
	echo "xyzzy" > note5 &&
	git notes edit -m spam -F note5 -m "foo
bar
baz"
'

whitespace="    "
cat > expect-m-and-F << EOF
commit 15023535574ded8b1a89052b32673f84cf9582b8
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:16:13 2005 -0700

    4th

Notes:
    spam
$whitespace
    xyzzy
$whitespace
    foo
    bar
    baz
EOF

printf "\n" >> expect-m-and-F
cat expect-multiline >> expect-m-and-F

test_expect_success 'show -m and -F notes' '
	git log -3 > output &&
	test_cmp expect-m-and-F output
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
    spam
$whitespace
    xyzzy
$whitespace
    foo
    bar
    baz
EOF
test_expect_success 'git log --show-notes' '
	git log -1 --pretty=raw --show-notes >output &&
	test_cmp expect output
'

test_expect_success 'git log --no-notes' '
	git log -1 --no-notes >output &&
	! grep spam output
'

test_expect_success 'git format-patch does not show notes' '
	git format-patch -1 --stdout >output &&
	! grep spam output
'

test_expect_success 'git format-patch --show-notes does show notes' '
	git format-patch --show-notes -1 --stdout >output &&
	grep spam output
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
		eval "$negate grep spam output"
	'
done

test_done
