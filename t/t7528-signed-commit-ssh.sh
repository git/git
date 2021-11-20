#!/bin/sh

test_description='ssh signed commit tests'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
GNUPGHOME_NOT_USED=$GNUPGHOME
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPGSSH 'create signed commits' '
	test_oid_cache <<-\EOF &&
	header sha1:gpgsig
	header sha256:gpgsig-sha256
	EOF

	test_when_finished "test_unconfig commit.gpgsign" &&
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&

	echo 1 >file && git add file &&
	test_tick && git commit -S -m initial &&
	git tag initial &&
	git branch side &&

	echo 2 >file && test_tick && git commit -a -S -m second &&
	git tag second &&

	git checkout side &&
	echo 3 >elif && git add elif &&
	test_tick && git commit -m "third on side" &&

	git checkout main &&
	test_tick && git merge -S side &&
	git tag merge &&

	echo 4 >file && test_tick && git commit -a -m "fourth unsigned" &&
	git tag fourth-unsigned &&

	test_tick && git commit --amend -S -m "fourth signed" &&
	git tag fourth-signed &&

	git config commit.gpgsign true &&
	echo 5 >file && test_tick && git commit -a -m "fifth signed" &&
	git tag fifth-signed &&

	git config commit.gpgsign false &&
	echo 6 >file && test_tick && git commit -a -m "sixth" &&
	git tag sixth-unsigned &&

	git config commit.gpgsign true &&
	echo 7 >file && test_tick && git commit -a -m "seventh" --no-gpg-sign &&
	git tag seventh-unsigned &&

	test_tick && git rebase -f HEAD^^ && git tag sixth-signed HEAD^ &&
	git tag seventh-signed &&

	echo 8 >file && test_tick && git commit -a -m eighth -S"${GPGSSH_KEY_UNTRUSTED}" &&
	git tag eighth-signed-alt &&

	# commit.gpgsign is still on but this must not be signed
	echo 9 | git commit-tree HEAD^{tree} >oid &&
	test_line_count = 1 oid &&
	git tag ninth-unsigned $(cat oid) &&
	# explicit -S of course must sign.
	echo 10 | git commit-tree -S HEAD^{tree} >oid &&
	test_line_count = 1 oid &&
	git tag tenth-signed $(cat oid) &&

	# --gpg-sign[=<key-id>] must sign.
	echo 11 | git commit-tree --gpg-sign HEAD^{tree} >oid &&
	test_line_count = 1 oid &&
	git tag eleventh-signed $(cat oid) &&
	echo 12 | git commit-tree --gpg-sign="${GPGSSH_KEY_UNTRUSTED}" HEAD^{tree} >oid &&
	test_line_count = 1 oid &&
	git tag twelfth-signed-alt $(cat oid)
'

test_expect_success GPGSSH 'verify and show signatures' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	test_config gpg.mintrustlevel UNDEFINED &&
	(
		for commit in initial second merge fourth-signed \
			fifth-signed sixth-signed seventh-signed tenth-signed \
			eleventh-signed
		do
			git verify-commit $commit &&
			git show --pretty=short --show-signature $commit >actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in merge^2 fourth-unsigned sixth-unsigned \
			seventh-unsigned ninth-unsigned
		do
			test_must_fail git verify-commit $commit &&
			git show --pretty=short --show-signature $commit >actual &&
			! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in eighth-signed-alt twelfth-signed-alt
		do
			git show --pretty=short --show-signature $commit >actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			grep "${GPGSSH_KEY_NOT_TRUSTED}" actual &&
			echo $commit OK || exit 1
		done
	)
'

test_expect_success GPGSSH 'verify-commit exits failure on untrusted signature' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	test_must_fail git verify-commit eighth-signed-alt 2>actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
	grep "${GPGSSH_KEY_NOT_TRUSTED}" actual
'

test_expect_success GPGSSH 'verify-commit exits success with matching minTrustLevel' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	test_config gpg.minTrustLevel fully &&
	git verify-commit sixth-signed
'

test_expect_success GPGSSH 'verify-commit exits success with low minTrustLevel' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	test_config gpg.minTrustLevel marginal &&
	git verify-commit sixth-signed
'

test_expect_success GPGSSH 'verify-commit exits failure with high minTrustLevel' '
	test_config gpg.minTrustLevel ultimate &&
	test_must_fail git verify-commit eighth-signed-alt
'

test_expect_success GPGSSH 'verify signatures with --raw' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	(
		for commit in initial second merge fourth-signed fifth-signed sixth-signed seventh-signed
		do
			git verify-commit --raw $commit 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in merge^2 fourth-unsigned sixth-unsigned seventh-unsigned
		do
			test_must_fail git verify-commit --raw $commit 2>actual &&
			! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in eighth-signed-alt
		do
			test_must_fail git verify-commit --raw $commit 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $commit OK || exit 1
		done
	)
'

test_expect_success GPGSSH 'proper header is used for hash algorithm' '
	git cat-file commit fourth-signed >output &&
	grep "^$(test_oid header) -----BEGIN SSH SIGNATURE-----" output
'

test_expect_success GPGSSH 'show signed commit with signature' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git show -s initial >commit &&
	git show -s --show-signature initial >show &&
	git verify-commit -v initial >verify.1 2>verify.2 &&
	git cat-file commit initial >cat &&
	grep -v -e "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" -e "Warning: " show >show.commit &&
	grep -e "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" -e "Warning: " show >show.gpg &&
	grep -v "^ " cat | grep -v "^gpgsig.* " >cat.commit &&
	test_cmp show.commit commit &&
	test_cmp show.gpg verify.2 &&
	test_cmp cat.commit verify.1
'

test_expect_success GPGSSH 'detect fudged signature' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git cat-file commit seventh-signed >raw &&
	sed -e "s/^seventh/7th forged/" raw >forged1 &&
	git hash-object -w -t commit forged1 >forged1.commit &&
	test_must_fail git verify-commit $(cat forged1.commit) &&
	git show --pretty=short --show-signature $(cat forged1.commit) >actual1 &&
	grep "${GPGSSH_BAD_SIGNATURE}" actual1 &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual1 &&
	! grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual1
'

test_expect_success GPGSSH 'detect fudged signature with NUL' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git cat-file commit seventh-signed >raw &&
	cat raw >forged2 &&
	echo Qwik | tr "Q" "\000" >>forged2 &&
	git hash-object -w -t commit forged2 >forged2.commit &&
	test_must_fail git verify-commit $(cat forged2.commit) &&
	git show --pretty=short --show-signature $(cat forged2.commit) >actual2 &&
	grep "${GPGSSH_BAD_SIGNATURE}" actual2 &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual2
'

test_expect_success GPGSSH 'amending already signed commit' '
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git checkout fourth-signed^0 &&
	git commit --amend -S --no-edit &&
	git verify-commit HEAD &&
	git show -s --show-signature HEAD >actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual
'

test_expect_success GPGSSH 'show good signature with custom format' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	FINGERPRINT=$(ssh-keygen -lf "${GPGSSH_KEY_PRIMARY}" | awk "{print \$2;}") &&
	cat >expect.tmpl <<-\EOF &&
	G
	FINGERPRINT
	principal with number 1
	FINGERPRINT

	EOF
	sed "s|FINGERPRINT|$FINGERPRINT|g" expect.tmpl >expect &&
	git log -1 --format="%G?%n%GK%n%GS%n%GF%n%GP" sixth-signed >actual &&
	test_cmp expect actual
'

test_expect_success GPGSSH 'show bad signature with custom format' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	cat >expect <<-\EOF &&
	B




	EOF
	git log -1 --format="%G?%n%GK%n%GS%n%GF%n%GP" $(cat forged1.commit) >actual &&
	test_cmp expect actual
'

test_expect_success GPGSSH 'show untrusted signature with custom format' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	cat >expect.tmpl <<-\EOF &&
	U
	FINGERPRINT

	FINGERPRINT

	EOF
	git log -1 --format="%G?%n%GK%n%GS%n%GF%n%GP" eighth-signed-alt >actual &&
	FINGERPRINT=$(ssh-keygen -lf "${GPGSSH_KEY_UNTRUSTED}" | awk "{print \$2;}") &&
	sed "s|FINGERPRINT|$FINGERPRINT|g" expect.tmpl >expect &&
	test_cmp expect actual
'

test_expect_success GPGSSH 'show untrusted signature with undefined trust level' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	cat >expect.tmpl <<-\EOF &&
	undefined
	FINGERPRINT

	FINGERPRINT

	EOF
	git log -1 --format="%GT%n%GK%n%GS%n%GF%n%GP" eighth-signed-alt >actual &&
	FINGERPRINT=$(ssh-keygen -lf "${GPGSSH_KEY_UNTRUSTED}" | awk "{print \$2;}") &&
	sed "s|FINGERPRINT|$FINGERPRINT|g" expect.tmpl >expect &&
	test_cmp expect actual
'

test_expect_success GPGSSH 'show untrusted signature with ultimate trust level' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	cat >expect.tmpl <<-\EOF &&
	fully
	FINGERPRINT
	principal with number 1
	FINGERPRINT

	EOF
	git log -1 --format="%GT%n%GK%n%GS%n%GF%n%GP" sixth-signed >actual &&
	FINGERPRINT=$(ssh-keygen -lf "${GPGSSH_KEY_PRIMARY}" | awk "{print \$2;}") &&
	sed "s|FINGERPRINT|$FINGERPRINT|g" expect.tmpl >expect &&
	test_cmp expect actual
'

test_expect_success GPGSSH 'show lack of signature with custom format' '
	cat >expect <<-\EOF &&
	N




	EOF
	git log -1 --format="%G?%n%GK%n%GS%n%GF%n%GP" seventh-unsigned >actual &&
	test_cmp expect actual
'

test_expect_success GPGSSH 'log.showsignature behaves like --show-signature' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	test_config log.showsignature true &&
	git show initial >actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH 'check config gpg.format values' '
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&
	test_config gpg.format ssh &&
	git commit -S --amend -m "success" &&
	test_config gpg.format OpEnPgP &&
	test_must_fail git commit -S --amend -m "fail"
'

test_expect_failure GPGSSH 'detect fudged commit with double signature (TODO)' '
	sed -e "/gpgsig/,/END PGP/d" forged1 >double-base &&
	sed -n -e "/gpgsig/,/END PGP/p" forged1 | \
		sed -e "s/^$(test_oid header)//;s/^ //" | gpg --dearmor >double-sig1.sig &&
	gpg -o double-sig2.sig -u 29472784 --detach-sign double-base &&
	cat double-sig1.sig double-sig2.sig | gpg --enarmor >double-combined.asc &&
	sed -e "s/^\(-.*\)ARMORED FILE/\1SIGNATURE/;1s/^/$(test_oid header) /;2,\$s/^/ /" \
		double-combined.asc > double-gpgsig &&
	sed -e "/committer/r double-gpgsig" double-base >double-commit &&
	git hash-object -w -t commit double-commit >double-commit.commit &&
	test_must_fail git verify-commit $(cat double-commit.commit) &&
	git show --pretty=short --show-signature $(cat double-commit.commit) >double-actual &&
	grep "BAD signature from" double-actual &&
	grep "Good signature from" double-actual
'

test_expect_failure GPGSSH 'show double signature with custom format (TODO)' '
	cat >expect <<-\EOF &&
	E




	EOF
	git log -1 --format="%G?%n%GK%n%GS%n%GF%n%GP" $(cat double-commit.commit) >actual &&
	test_cmp expect actual
'


test_expect_failure GPGSSH 'verify-commit verifies multiply signed commits (TODO)' '
	git init multiply-signed &&
	cd multiply-signed &&
	test_commit first &&
	echo 1 >second &&
	git add second &&
	tree=$(git write-tree) &&
	parent=$(git rev-parse HEAD^{commit}) &&
	git commit --gpg-sign -m second &&
	git cat-file commit HEAD &&
	# Avoid trailing whitespace.
	sed -e "s/^Q//" -e "s/^Z/ /" >commit <<-EOF &&
	Qtree $tree
	Qparent $parent
	Qauthor A U Thor <author@example.com> 1112912653 -0700
	Qcommitter C O Mitter <committer@example.com> 1112912653 -0700
	Qgpgsig -----BEGIN PGP SIGNATURE-----
	QZ
	Q iHQEABECADQWIQRz11h0S+chaY7FTocTtvUezd5DDQUCX/uBDRYcY29tbWl0dGVy
	Q QGV4YW1wbGUuY29tAAoJEBO29R7N3kMNd+8AoK1I8mhLHviPH+q2I5fIVgPsEtYC
	Q AKCTqBh+VabJceXcGIZuF0Ry+udbBQ==
	Q =tQ0N
	Q -----END PGP SIGNATURE-----
	Qgpgsig-sha256 -----BEGIN PGP SIGNATURE-----
	QZ
	Q iHQEABECADQWIQRz11h0S+chaY7FTocTtvUezd5DDQUCX/uBIBYcY29tbWl0dGVy
	Q QGV4YW1wbGUuY29tAAoJEBO29R7N3kMN/NEAn0XO9RYSBj2dFyozi0JKSbssYMtO
	Q AJwKCQ1BQOtuwz//IjU8TiS+6S4iUw==
	Q =pIwP
	Q -----END PGP SIGNATURE-----
	Q
	Qsecond
	EOF
	head=$(git hash-object -t commit -w commit) &&
	git reset --hard $head &&
	git verify-commit $head 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual
'

test_done
