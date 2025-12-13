#!/bin/sh

test_description='git fast-import --signed-commits=<mode>'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success 'set up unsigned initial commit and import repo' '
	test_commit first &&
	git init new
'

test_expect_success GPG 'set up OpenPGP signed commit' '
	git checkout -b openpgp-signing main &&
	echo "Content for OpenPGP signing." >file-sign &&
	git add file-sign &&
	git commit -S -m "OpenPGP signed commit" &&
	OPENPGP_SIGNING=$(git rev-parse --verify openpgp-signing)
'

test_expect_success GPG 'import OpenPGP signature with --signed-commits=verbatim' '
	git fast-export --signed-commits=verbatim openpgp-signing >output &&
	git -C new fast-import --quiet --signed-commits=verbatim <output >log 2>&1 &&
	IMPORTED=$(git -C new rev-parse --verify refs/heads/openpgp-signing) &&
	test $OPENPGP_SIGNING = $IMPORTED &&
	test_must_be_empty log
'

test_expect_success GPGSM 'set up X.509 signed commit' '
	git checkout -b x509-signing main &&
	test_config gpg.format x509 &&
	test_config user.signingkey $GIT_COMMITTER_EMAIL &&
	echo "Content for X.509 signing." >file-sign &&
	git add file-sign &&
	git commit -S -m "X.509 signed commit" &&
	X509_SIGNING=$(git rev-parse HEAD)
'

test_expect_success GPGSM 'import X.509 signature fails with --signed-commits=abort' '
	git fast-export --signed-commits=verbatim x509-signing >output &&
	test_must_fail git -C new fast-import --quiet --signed-commits=abort <output
'

test_expect_success GPGSM 'import X.509 signature with --signed-commits=warn-verbatim' '
	git -C new fast-import --quiet --signed-commits=warn-verbatim <output >log 2>&1 &&
	IMPORTED=$(git -C new rev-parse --verify refs/heads/x509-signing) &&
	test $X509_SIGNING = $IMPORTED &&
	test_grep "importing a commit signature" log
'

test_expect_success GPGSSH 'set up SSH signed commit' '
	git checkout -b ssh-signing main &&
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&
	echo "Content for SSH signing." >file-sign &&
	git add file-sign &&
	git commit -S -m "SSH signed commit" &&
	SSH_SIGNING=$(git rev-parse HEAD)
'

test_expect_success GPGSSH 'strip SSH signature with --signed-commits=strip' '
	git fast-export --signed-commits=verbatim ssh-signing >output &&
	git -C new fast-import --quiet --signed-commits=strip <output >log 2>&1 &&
	IMPORTED=$(git -C new rev-parse --verify refs/heads/ssh-signing) &&
	test $SSH_SIGNING != $IMPORTED &&
	git -C new cat-file commit "$IMPORTED" >actual &&
	test_grep ! -E "^gpgsig" actual &&
	test_must_be_empty log
'

test_expect_success RUST,GPG 'setup a commit with dual OpenPGP signatures on its SHA-1 and SHA-256 formats' '
	# Create a signed SHA-256 commit
	git init --object-format=sha256 explicit-sha256 &&
	git -C explicit-sha256 config extensions.compatObjectFormat sha1 &&
	git -C explicit-sha256 checkout -b dual-signed &&
	test_commit -C explicit-sha256 A &&
	echo B >explicit-sha256/B &&
	git -C explicit-sha256 add B &&
	test_tick &&
	git -C explicit-sha256 commit -S -m "signed commit" B &&
	SHA256_B=$(git -C explicit-sha256 rev-parse dual-signed) &&

	# Create the corresponding SHA-1 commit
	SHA1_B=$(git -C explicit-sha256 rev-parse --output-object-format=sha1 dual-signed) &&

	# Check that the resulting SHA-1 commit has both signatures
	git -C explicit-sha256 cat-file -p $SHA1_B >out &&
	test_grep -E "^gpgsig " out &&
	test_grep -E "^gpgsig-sha256 " out
'

test_expect_success RUST,GPG 'strip both OpenPGP signatures with --signed-commits=warn-strip' '
	git -C explicit-sha256 fast-export --signed-commits=verbatim dual-signed >output &&
	test_grep -E "^gpgsig sha1 openpgp" output &&
	test_grep -E "^gpgsig sha256 openpgp" output &&
	git -C new fast-import --quiet --signed-commits=warn-strip <output >log 2>&1 &&
	git -C new cat-file commit refs/heads/dual-signed >actual &&
	test_grep ! -E "^gpgsig " actual &&
	test_grep ! -E "^gpgsig-sha256 " actual &&
	test_grep "stripping a commit signature" log >out &&
	test_line_count = 2 out
'

test_expect_success GPG 'import commit with no signature with --signed-commits=strip-if-invalid' '
	git fast-export main >output &&
	git -C new fast-import --quiet --signed-commits=strip-if-invalid <output >log 2>&1 &&
	test_must_be_empty log
'

test_expect_success GPG 'keep valid OpenPGP signature with --signed-commits=strip-if-invalid' '
	rm -rf new &&
	git init new &&

	git fast-export --signed-commits=verbatim openpgp-signing >output &&
	git -C new fast-import --quiet --signed-commits=strip-if-invalid <output >log 2>&1 &&
	IMPORTED=$(git -C new rev-parse --verify refs/heads/openpgp-signing) &&
	test $OPENPGP_SIGNING = $IMPORTED &&
	git -C new cat-file commit "$IMPORTED" >actual &&
	test_grep -E "^gpgsig(-sha256)? " actual &&
	test_must_be_empty log
'

test_expect_success GPG 'strip signature invalidated by message change with --signed-commits=strip-if-invalid' '
	rm -rf new &&
	git init new &&

	git fast-export --signed-commits=verbatim openpgp-signing >output &&

	# Change the commit message, which invalidates the signature.
	# The commit message length should not change though, otherwise the
	# corresponding `data <length>` command would have to be changed too.
	sed "s/OpenPGP signed commit/OpenPGP forged commit/" output >modified &&

	git -C new fast-import --quiet --signed-commits=strip-if-invalid <modified >log 2>&1 &&

	IMPORTED=$(git -C new rev-parse --verify refs/heads/openpgp-signing) &&
	test $OPENPGP_SIGNING != $IMPORTED &&
	git -C new cat-file commit "$IMPORTED" >actual &&
	test_grep ! -E "^gpgsig" actual &&
	test_grep "stripping invalid signature" log
'

test_expect_success GPGSM 'keep valid X.509 signature with --signed-commits=strip-if-invalid' '
	rm -rf new &&
	git init new &&

	git fast-export --signed-commits=verbatim x509-signing >output &&
	git -C new fast-import --quiet --signed-commits=strip-if-invalid <output >log 2>&1 &&
	IMPORTED=$(git -C new rev-parse --verify refs/heads/x509-signing) &&
	test $X509_SIGNING = $IMPORTED &&
	git -C new cat-file commit "$IMPORTED" >actual &&
	test_grep -E "^gpgsig(-sha256)? " actual &&
	test_must_be_empty log
'

test_expect_success GPGSSH 'keep valid SSH signature with --signed-commits=strip-if-invalid' '
	rm -rf new &&
	git init new &&

	test_config -C new gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&

	git fast-export --signed-commits=verbatim ssh-signing >output &&
	git -C new fast-import --quiet --signed-commits=strip-if-invalid <output >log 2>&1 &&
	IMPORTED=$(git -C new rev-parse --verify refs/heads/ssh-signing) &&
	test $SSH_SIGNING = $IMPORTED &&
	git -C new cat-file commit "$IMPORTED" >actual &&
	test_grep -E "^gpgsig(-sha256)? " actual &&
	test_must_be_empty log
'

test_done
