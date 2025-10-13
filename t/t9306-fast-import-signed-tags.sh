#!/bin/sh

test_description='git fast-import --signed-tags=<mode>'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success 'set up unsigned initial commit and import repo' '
	test_commit first &&
	git init new
'

test_expect_success 'import no signed tag with --signed-tags=abort' '
	git fast-export --signed-tags=verbatim >output &&
	git -C new fast-import --quiet --signed-tags=abort <output
'

test_expect_success GPG 'set up OpenPGP signed tag' '
	git tag -s -m "OpenPGP signed tag" openpgp-signed first &&
	OPENPGP_SIGNED=$(git rev-parse --verify refs/tags/openpgp-signed) &&
	git fast-export --signed-tags=verbatim openpgp-signed >output
'

test_expect_success GPG 'import OpenPGP signed tag with --signed-tags=abort' '
	test_must_fail git -C new fast-import --quiet --signed-tags=abort <output
'

test_expect_success GPG 'import OpenPGP signed tag with --signed-tags=verbatim' '
	git -C new fast-import --quiet --signed-tags=verbatim <output >log 2>&1 &&
	IMPORTED=$(git -C new rev-parse --verify refs/tags/openpgp-signed) &&
	test $OPENPGP_SIGNED = $IMPORTED &&
	test_must_be_empty log
'

test_expect_success GPGSM 'setup X.509 signed tag' '
	test_config gpg.format x509 &&
	test_config user.signingkey $GIT_COMMITTER_EMAIL &&

	git tag -s -m "X.509 signed tag" x509-signed first &&
	X509_SIGNED=$(git rev-parse --verify refs/tags/x509-signed) &&
	git fast-export --signed-tags=verbatim x509-signed >output
'

test_expect_success GPGSM 'import X.509 signed tag with --signed-tags=warn-strip' '
	git -C new fast-import --quiet --signed-tags=warn-strip <output >log 2>&1 &&
	test_grep "stripping a tag signature for tag '\''x509-signed'\''" log &&
	IMPORTED=$(git -C new rev-parse --verify refs/tags/x509-signed) &&
	test $X509_SIGNED != $IMPORTED &&
	git -C new cat-file -p x509-signed >out &&
	test_grep ! "SIGNED MESSAGE" out
'

test_expect_success GPGSSH 'setup SSH signed tag' '
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&

	git tag -s -m "SSH signed tag" ssh-signed first &&
	SSH_SIGNED=$(git rev-parse --verify refs/tags/ssh-signed) &&
	git fast-export --signed-tags=verbatim ssh-signed >output
'

test_expect_success GPGSSH 'import SSH signed tag with --signed-tags=warn-verbatim' '
	git -C new fast-import --quiet --signed-tags=warn-verbatim <output >log 2>&1 &&
	test_grep "importing a tag signature verbatim for tag '\''ssh-signed'\''" log &&
	IMPORTED=$(git -C new rev-parse --verify refs/tags/ssh-signed) &&
	test $SSH_SIGNED = $IMPORTED
'

test_expect_success GPGSSH 'import SSH signed tag with --signed-tags=strip' '
	git -C new fast-import --quiet --signed-tags=strip <output >log 2>&1 &&
	test_must_be_empty log &&
	IMPORTED=$(git -C new rev-parse --verify refs/tags/ssh-signed) &&
	test $SSH_SIGNED != $IMPORTED &&
	git -C new cat-file -p ssh-signed >out &&
	test_grep ! "SSH SIGNATURE" out
'

test_done
