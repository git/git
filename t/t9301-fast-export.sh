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
		 ! git rev-parse partial~2)

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

	! git fast-export --signed-tags=abort sign-your-name

'

test_expect_success 'signed-tags=verbatim' '

	git fast-export --signed-tags=verbatim sign-your-name > output &&
	grep PGP output

'

test_expect_success 'signed-tags=strip' '

	git fast-export --signed-tags=strip sign-your-name > output &&
	! grep PGP output

'

test_done
