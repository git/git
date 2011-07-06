#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='git-fast-export'
. ./test-lib.sh

test_expect_success 'setup' '

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
	echo lange > file2
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
		 git diff master..partial &&
		 git diff master^..partial^ &&
		 test_must_fail git rev-parse partial~2)

'

test_expect_success 'iso-8859-1' '

	git config i18n.commitencoding ISO-8859-1 &&
	# use author and committer name in ISO-8859-1 to match it.
	. ../t3901-8859-1.txt &&
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
	test $(wc -l < tmp-marks) -eq 3 &&
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
	test $(wc -l < tmp-marks) -eq 4

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

test_expect_success 'setup submodule' '

	git checkout -f master &&
	mkdir sub &&
	cd sub &&
	git init  &&
	echo test file > file &&
	git add file &&
	git commit -m sub_initial &&
	cd .. &&
	git submodule add "`pwd`/sub" sub &&
	git commit -m initial &&
	test_tick &&
	cd sub &&
	echo more data >> file &&
	git add file &&
	git commit -m sub_second &&
	cd .. &&
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

export GIT_AUTHOR_NAME='A U Thor'
export GIT_COMMITTER_NAME='C O Mitter'

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
	grep "^C \"file6\" \"file7\"\$" output &&
	cat output |
	(cd new &&
	 git fast-import &&
	 test $ENTRY = $(git rev-parse --verify refs/heads/copy))

'

test_done
