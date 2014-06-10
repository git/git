#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='git fast-export'
. ./test-lib.sh

test_expect_success 'setup' '

	echo break it > file0 &&
	git add file0 &&
	test_tick &&
	echo Wohlauf > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	echo die Luft > file &&
	echo geht frisch > file2 &&
	git add file file2 &&
	test_tick &&
	git commit -m second &&
	echo und > file2 &&
	test_tick &&
	git commit -m third file2 &&
	test_tick &&
	git tag rein &&
	git checkout -b wer HEAD^ &&
	echo lange > file2 &&
	test_tick &&
	git commit -m sitzt file2 &&
	test_tick &&
	git tag -a -m valentin muss &&
	git merge -s ours master

'

test_expect_success 'fast-export | fast-import' '

	MASTER=$(git rev-parse --verify master) &&
	REIN=$(git rev-parse --verify rein) &&
	WER=$(git rev-parse --verify wer) &&
	MUSS=$(git rev-parse --verify muss) &&
	mkdir new &&
	git --git-dir=new/.git init &&
	git fast-export --all |
	(cd new &&
	 git fast-import &&
	 test $MASTER = $(git rev-parse --verify refs/heads/master) &&
	 test $REIN = $(git rev-parse --verify refs/tags/rein) &&
	 test $WER = $(git rev-parse --verify refs/heads/wer) &&
	 test $MUSS = $(git rev-parse --verify refs/tags/muss))

'

test_expect_success 'fast-export master~2..master' '

	git fast-export master~2..master |
		sed "s/master/partial/" |
		(cd new &&
		 git fast-import &&
		 test $MASTER != $(git rev-parse --verify refs/heads/partial) &&
		 git diff --exit-code master partial &&
		 git diff --exit-code master^ partial^ &&
		 test_must_fail git rev-parse partial~2)

'

test_expect_success 'iso-8859-1' '

	git config i18n.commitencoding ISO8859-1 &&
	# use author and committer name in ISO-8859-1 to match it.
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&
	test_tick &&
	echo rosten >file &&
	git commit -s -m den file &&
	git fast-export wer^..wer |
		sed "s/wer/i18n/" |
		(cd new &&
		 git fast-import &&
		 git cat-file commit i18n | grep "Áéí óú")

'
test_expect_success 'import/export-marks' '

	git checkout -b marks master &&
	git fast-export --export-marks=tmp-marks HEAD &&
	test -s tmp-marks &&
	test_line_count = 3 tmp-marks &&
	test $(
		git fast-export --import-marks=tmp-marks\
		--export-marks=tmp-marks HEAD |
		grep ^commit |
		wc -l) \
	-eq 0 &&
	echo change > file &&
	git commit -m "last commit" file &&
	test $(
		git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks HEAD |
		grep ^commit\  |
		wc -l) \
	-eq 1 &&
	test_line_count = 4 tmp-marks

'

cat > signed-tag-import << EOF
tag sign-your-name
from $(git rev-parse HEAD)
tagger C O Mitter <committer@example.com> 1112911993 -0700
data 210
A message for a sign
-----BEGIN PGP SIGNATURE-----
Version: GnuPG v1.4.5 (GNU/Linux)

fakedsignaturefakedsignaturefakedsignaturefakedsignaturfakedsign
aturefakedsignaturefake=
=/59v
-----END PGP SIGNATURE-----
EOF

test_expect_success 'set up faked signed tag' '

	cat signed-tag-import | git fast-import

'

test_expect_success 'signed-tags=abort' '

	test_must_fail git fast-export --signed-tags=abort sign-your-name

'

test_expect_success 'signed-tags=verbatim' '

	git fast-export --signed-tags=verbatim sign-your-name > output &&
	grep PGP output

'

test_expect_success 'signed-tags=strip' '

	git fast-export --signed-tags=strip sign-your-name > output &&
	! grep PGP output

'

test_expect_success 'signed-tags=warn-strip' '
	git fast-export --signed-tags=warn-strip sign-your-name >output 2>err &&
	! grep PGP output &&
	test -s err
'

test_expect_success 'setup submodule' '

	git checkout -f master &&
	mkdir sub &&
	(
		cd sub &&
		git init  &&
		echo test file > file &&
		git add file &&
		git commit -m sub_initial
	) &&
	git submodule add "`pwd`/sub" sub &&
	git commit -m initial &&
	test_tick &&
	(
		cd sub &&
		echo more data >> file &&
		git add file &&
		git commit -m sub_second
	) &&
	git add sub &&
	git commit -m second

'

test_expect_success 'submodule fast-export | fast-import' '

	SUBENT1=$(git ls-tree master^ sub) &&
	SUBENT2=$(git ls-tree master sub) &&
	rm -rf new &&
	mkdir new &&
	git --git-dir=new/.git init &&
	git fast-export --signed-tags=strip --all |
	(cd new &&
	 git fast-import &&
	 test "$SUBENT1" = "$(git ls-tree refs/heads/master^ sub)" &&
	 test "$SUBENT2" = "$(git ls-tree refs/heads/master sub)" &&
	 git checkout master &&
	 git submodule init &&
	 git submodule update &&
	 cmp sub/file ../sub/file)

'

GIT_AUTHOR_NAME='A U Thor'; export GIT_AUTHOR_NAME
GIT_COMMITTER_NAME='C O Mitter'; export GIT_COMMITTER_NAME

test_expect_success 'setup copies' '

	git config --unset i18n.commitencoding &&
	git checkout -b copy rein &&
	git mv file file3 &&
	git commit -m move1 &&
	test_tick &&
	cp file2 file4 &&
	git add file4 &&
	git mv file2 file5 &&
	git commit -m copy1 &&
	test_tick &&
	cp file3 file6 &&
	git add file6 &&
	git commit -m copy2 &&
	test_tick &&
	echo more text >> file6 &&
	echo even more text >> file6 &&
	git add file6 &&
	git commit -m modify &&
	test_tick &&
	cp file6 file7 &&
	echo test >> file7 &&
	git add file7 &&
	git commit -m copy_modify

'

test_expect_success 'fast-export -C -C | fast-import' '

	ENTRY=$(git rev-parse --verify copy) &&
	rm -rf new &&
	mkdir new &&
	git --git-dir=new/.git init &&
	git fast-export -C -C --signed-tags=strip --all > output &&
	grep "^C file6 file7\$" output &&
	cat output |
	(cd new &&
	 git fast-import &&
	 test $ENTRY = $(git rev-parse --verify refs/heads/copy))

'

test_expect_success 'fast-export | fast-import when master is tagged' '

	git tag -m msg last &&
	git fast-export -C -C --signed-tags=strip --all > output &&
	test $(grep -c "^tag " output) = 3

'

cat > tag-content << EOF
object $(git rev-parse HEAD)
type commit
tag rosten
EOF

test_expect_success 'cope with tagger-less tags' '

	TAG=$(git hash-object -t tag -w tag-content) &&
	git update-ref refs/tags/sonnenschein $TAG &&
	git fast-export -C -C --signed-tags=strip --all > output &&
	test $(grep -c "^tag " output) = 4 &&
	! grep "Unspecified Tagger" output &&
	git fast-export -C -C --signed-tags=strip --all \
		--fake-missing-tagger > output &&
	test $(grep -c "^tag " output) = 4 &&
	grep "Unspecified Tagger" output

'

test_expect_success 'setup for limiting exports by PATH' '
	mkdir limit-by-paths &&
	(
		cd limit-by-paths &&
		git init &&
		echo hi > there &&
		git add there &&
		git commit -m "First file" &&
		echo foo > bar &&
		git add bar &&
		git commit -m "Second file" &&
		git tag -a -m msg mytag &&
		echo morefoo >> bar &&
		git add bar &&
		git commit -m "Change to second file"
	)
'

cat > limit-by-paths/expected << EOF
blob
mark :1
data 3
hi

reset refs/tags/mytag
commit refs/tags/mytag
mark :2
author A U Thor <author@example.com> 1112912713 -0700
committer C O Mitter <committer@example.com> 1112912713 -0700
data 11
First file
M 100644 :1 there

EOF

test_expect_success 'dropping tag of filtered out object' '
(
	cd limit-by-paths &&
	git fast-export --tag-of-filtered-object=drop mytag -- there > output &&
	test_cmp expected output
)
'

cat >> limit-by-paths/expected << EOF
tag mytag
from :2
tagger C O Mitter <committer@example.com> 1112912713 -0700
data 4
msg

EOF

test_expect_success 'rewriting tag of filtered out object' '
(
	cd limit-by-paths &&
	git fast-export --tag-of-filtered-object=rewrite mytag -- there > output &&
	test_cmp expected output
)
'

cat > limit-by-paths/expected << EOF
blob
mark :1
data 4
foo

blob
mark :2
data 3
hi

reset refs/heads/master
commit refs/heads/master
mark :3
author A U Thor <author@example.com> 1112912713 -0700
committer C O Mitter <committer@example.com> 1112912713 -0700
data 12
Second file
M 100644 :1 bar
M 100644 :2 there

EOF

test_expect_failure 'no exact-ref revisions included' '
	(
		cd limit-by-paths &&
		git fast-export master~2..master~1 > output &&
		test_cmp expected output
	)
'

test_expect_success 'path limiting with import-marks does not lose unmodified files'        '
	git checkout -b simple marks~2 &&
	git fast-export --export-marks=marks simple -- file > /dev/null &&
	echo more content >> file &&
	test_tick &&
	git commit -mnext file &&
	git fast-export --import-marks=marks simple -- file file0 | grep file0
'

test_expect_success 'full-tree re-shows unmodified files'        '
	git checkout -f simple &&
	test $(git fast-export --full-tree simple | grep -c file0) -eq 3
'

test_expect_success 'set-up a few more tags for tag export tests' '
	git checkout -f master &&
	HEAD_TREE=`git show -s --pretty=raw HEAD | grep tree | sed "s/tree //"` &&
	git tag    tree_tag        -m "tagging a tree" $HEAD_TREE &&
	git tag -a tree_tag-obj    -m "tagging a tree" $HEAD_TREE &&
	git tag    tag-obj_tag     -m "tagging a tag" tree_tag-obj &&
	git tag -a tag-obj_tag-obj -m "tagging a tag" tree_tag-obj
'

test_expect_success 'tree_tag'        '
	mkdir result &&
	(cd result && git init) &&
	git fast-export tree_tag > fe-stream &&
	(cd result && git fast-import < ../fe-stream)
'

# NEEDSWORK: not just check return status, but validate the output
test_expect_success 'tree_tag-obj'    'git fast-export tree_tag-obj'
test_expect_success 'tag-obj_tag'     'git fast-export tag-obj_tag'
test_expect_success 'tag-obj_tag-obj' 'git fast-export tag-obj_tag-obj'

test_expect_success 'directory becomes symlink'        '
	git init dirtosymlink &&
	git init result &&
	(
		cd dirtosymlink &&
		mkdir foo &&
		mkdir bar &&
		echo hello > foo/world &&
		echo hello > bar/world &&
		git add foo/world bar/world &&
		git commit -q -mone &&
		git rm -r foo &&
		test_ln_s_add bar foo &&
		git commit -q -mtwo
	) &&
	(
		cd dirtosymlink &&
		git fast-export master -- foo |
		(cd ../result && git fast-import --quiet)
	) &&
	(cd result && git show master:foo)
'

test_expect_success 'fast-export quotes pathnames' '
	git init crazy-paths &&
	(cd crazy-paths &&
	 blob=`echo foo | git hash-object -w --stdin` &&
	 git update-index --add \
		--cacheinfo 100644 $blob "$(printf "path with\\nnewline")" \
		--cacheinfo 100644 $blob "path with \"quote\"" \
		--cacheinfo 100644 $blob "path with \\backslash" \
		--cacheinfo 100644 $blob "path with space" &&
	 git commit -m addition &&
	 git ls-files -z -s | perl -0pe "s{\\t}{$&subdir/}" >index &&
	 git read-tree --empty &&
	 git update-index -z --index-info <index &&
	 git commit -m rename &&
	 git read-tree --empty &&
	 git commit -m deletion &&
	 git fast-export -M HEAD >export.out &&
	 git rev-list HEAD >expect &&
	 git init result &&
	 cd result &&
	 git fast-import <../export.out &&
	 git rev-list HEAD >actual &&
	 test_cmp ../expect actual
	)
'

test_expect_success 'test bidirectionality' '
	>marks-cur &&
	>marks-new &&
	git init marks-test &&
	git fast-export --export-marks=marks-cur --import-marks=marks-cur --branches | \
	git --git-dir=marks-test/.git fast-import --export-marks=marks-new --import-marks=marks-new &&
	(cd marks-test &&
	git reset --hard &&
	echo Wohlauf > file &&
	git commit -a -m "back in time") &&
	git --git-dir=marks-test/.git fast-export --export-marks=marks-new --import-marks=marks-new --branches | \
	git fast-import --export-marks=marks-cur --import-marks=marks-cur
'

cat > expected << EOF
blob
mark :13
data 5
bump

commit refs/heads/master
mark :14
author A U Thor <author@example.com> 1112912773 -0700
committer C O Mitter <committer@example.com> 1112912773 -0700
data 5
bump
from :12
M 100644 :13 file

EOF

test_expect_success 'avoid uninteresting refs' '
	> tmp-marks &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks master > /dev/null &&
	git tag v1.0 &&
	git branch uninteresting &&
	echo bump > file &&
	git commit -a -m bump &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks ^uninteresting ^v1.0 master > actual &&
	test_cmp expected actual
'

cat > expected << EOF
reset refs/heads/master
from :14

EOF

test_expect_success 'refs are updated even if no commits need to be exported' '
	> tmp-marks &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks master > /dev/null &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks master > actual &&
	test_cmp expected actual
'

test_expect_success 'use refspec' '
	git fast-export --refspec refs/heads/master:refs/heads/foobar master | \
		grep "^commit " | sort | uniq > actual &&
	echo "commit refs/heads/foobar" > expected &&
	test_cmp expected actual
'

test_expect_success 'delete refspec' '
	git branch to-delete &&
	git fast-export --refspec :refs/heads/to-delete to-delete ^to-delete > actual &&
	cat > expected <<-EOF &&
	reset refs/heads/to-delete
	from 0000000000000000000000000000000000000000

	EOF
	test_cmp expected actual
'

test_done
