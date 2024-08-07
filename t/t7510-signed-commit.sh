#!/bin/sh

test_description='signed commit tests'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
GNUPGHOME_NOT_USED=$GNUPGHOME
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPG 'create signed commits' '
	test_oid_cache <<-\EOF &&
	header sha1:gpgsig
	header sha256:gpgsig-sha256
	EOF

	test_when_finished "test_unconfig commit.gpgsign" &&

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

	echo 8 >file && test_tick && git commit -a -m eighth -SB7227189 &&
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
	echo 12 | git commit-tree --gpg-sign=B7227189 HEAD^{tree} >oid &&
	test_line_count = 1 oid &&
	git tag twelfth-signed-alt $(cat oid)
'

test_expect_success GPG 'verify and show signatures' '
	(
		for commit in initial second merge fourth-signed \
			fifth-signed sixth-signed seventh-signed tenth-signed \
			eleventh-signed
		do
			git verify-commit $commit &&
			git show --pretty=short --show-signature $commit >actual &&
			grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in merge^2 fourth-unsigned sixth-unsigned \
			seventh-unsigned ninth-unsigned
		do
			test_must_fail git verify-commit $commit &&
			git show --pretty=short --show-signature $commit >actual &&
			! grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in eighth-signed-alt twelfth-signed-alt
		do
			git show --pretty=short --show-signature $commit >actual &&
			grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			grep "not certified" actual &&
			echo $commit OK || exit 1
		done
	)
'

test_expect_success GPG 'verify-commit exits failure on unknown signature' '
	test_must_fail env GNUPGHOME="$GNUPGHOME_NOT_USED" git verify-commit initial 2>actual &&
	! grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	grep -q -F -e "No public key" -e "public key not found" actual
'

test_expect_success GPG 'verify-commit exits success on untrusted signature' '
	git verify-commit eighth-signed-alt 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	grep "not certified" actual
'

test_expect_success GPG 'verify-commit exits success with matching minTrustLevel' '
	test_config gpg.minTrustLevel ultimate &&
	git verify-commit sixth-signed
'

test_expect_success GPG 'verify-commit exits success with low minTrustLevel' '
	test_config gpg.minTrustLevel fully &&
	git verify-commit sixth-signed
'

test_expect_success GPG 'verify-commit exits failure with high minTrustLevel' '
	test_config gpg.minTrustLevel ultimate &&
	test_must_fail git verify-commit eighth-signed-alt
'

test_expect_success GPG 'verify signatures with --raw' '
	(
		for commit in initial second merge fourth-signed fifth-signed sixth-signed seventh-signed
		do
			git verify-commit --raw $commit 2>actual &&
			grep "GOODSIG" actual &&
			! grep "BADSIG" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in merge^2 fourth-unsigned sixth-unsigned seventh-unsigned
		do
			test_must_fail git verify-commit --raw $commit 2>actual &&
			! grep "GOODSIG" actual &&
			! grep "BADSIG" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in eighth-signed-alt
		do
			git verify-commit --raw $commit 2>actual &&
			grep "GOODSIG" actual &&
			! grep "BADSIG" actual &&
			grep "TRUST_UNDEFINED" actual &&
			echo $commit OK || exit 1
		done
	)
'

test_expect_success GPG 'proper header is used for hash algorithm' '
	git cat-file commit fourth-signed >output &&
	grep "^$(test_oid header) -----BEGIN PGP SIGNATURE-----" output
'

test_expect_success GPG 'show signed commit with signature' '
	git show -s initial >commit &&
	git show -s --show-signature initial >show &&
	git verify-commit -v initial >verify.1 2>verify.2 &&
	git cat-file commit initial >cat &&
	grep -v -e "gpg: " -e "Warning: " show >show.commit &&
	grep -e "gpg: " -e "Warning: " show >show.gpg &&
	grep -v "^ " cat | grep -v "^gpgsig.* " >cat.commit &&
	test_cmp show.commit commit &&
	test_cmp show.gpg verify.2 &&
	test_cmp cat.commit verify.1
'

test_expect_success GPG 'detect fudged signature' '
	git cat-file commit seventh-signed >raw &&
	sed -e "s/^seventh/7th forged/" raw >forged1 &&
	git hash-object -w -t commit forged1 >forged1.commit &&
	test_must_fail git verify-commit $(cat forged1.commit) &&
	git show --pretty=short --show-signature $(cat forged1.commit) >actual1 &&
	grep "BAD signature from" actual1 &&
	! grep "Good signature from" actual1
'

test_expect_success GPG 'detect fudged signature with NUL' '
	git cat-file commit seventh-signed >raw &&
	cat raw >forged2 &&
	echo Qwik | tr "Q" "\000" >>forged2 &&
	git hash-object --literally -w -t commit forged2 >forged2.commit &&
	test_must_fail git verify-commit $(cat forged2.commit) &&
	git show --pretty=short --show-signature $(cat forged2.commit) >actual2 &&
	grep "BAD signature from" actual2 &&
	! grep "Good signature from" actual2
'

test_expect_success GPG 'amending already signed commit' '
	git checkout -f fourth-signed^0 &&
	git commit --amend -S --no-edit &&
	git verify-commit HEAD &&
	git show -s --show-signature HEAD >actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual
'

test_expect_success GPG2 'bare signature' '
	git verify-commit fifth-signed 2>expect &&
	echo >>expect &&
	git log -1 --format="%GG" fifth-signed >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show good signature with custom format' '
	cat >expect <<-\EOF &&
	G
	ultimate
	13B6F51ECDDE430D
	C O Mitter <committer@example.com>
	73D758744BE721698EC54E8713B6F51ECDDE430D
	73D758744BE721698EC54E8713B6F51ECDDE430D
	EOF
	git log -1 --format="%G?%n%GT%n%GK%n%GS%n%GF%n%GP" sixth-signed >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show bad signature with custom format' '
	cat >expect <<-\EOF &&
	B
	undefined
	13B6F51ECDDE430D
	C O Mitter <committer@example.com>


	EOF
	git log -1 --format="%G?%n%GT%n%GK%n%GS%n%GF%n%GP" $(cat forged1.commit) >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show untrusted signature with custom format' '
	cat >expect <<-\EOF &&
	U
	undefined
	65A0EEA02E30CAD7
	Eris Discordia <discord@example.net>
	F8364A59E07FFE9F4D63005A65A0EEA02E30CAD7
	D4BE22311AD3131E5EDA29A461092E85B7227189
	EOF
	git log -1 --format="%G?%n%GT%n%GK%n%GS%n%GF%n%GP" eighth-signed-alt >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show untrusted signature with undefined trust level' '
	cat >expect <<-\EOF &&
	U
	undefined
	65A0EEA02E30CAD7
	Eris Discordia <discord@example.net>
	F8364A59E07FFE9F4D63005A65A0EEA02E30CAD7
	D4BE22311AD3131E5EDA29A461092E85B7227189
	EOF
	git log -1 --format="%G?%n%GT%n%GK%n%GS%n%GF%n%GP" eighth-signed-alt >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show untrusted signature with ultimate trust level' '
	cat >expect <<-\EOF &&
	G
	ultimate
	13B6F51ECDDE430D
	C O Mitter <committer@example.com>
	73D758744BE721698EC54E8713B6F51ECDDE430D
	73D758744BE721698EC54E8713B6F51ECDDE430D
	EOF
	git log -1 --format="%G?%n%GT%n%GK%n%GS%n%GF%n%GP" sixth-signed >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show unknown signature with custom format' '
	cat >expect <<-\EOF &&
	E
	undefined
	65A0EEA02E30CAD7



	EOF
	GNUPGHOME="$GNUPGHOME_NOT_USED" git log -1 --format="%G?%n%GT%n%GK%n%GS%n%GF%n%GP" eighth-signed-alt >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show lack of signature with custom format' '
	cat >expect <<-\EOF &&
	N
	undefined




	EOF
	git log -1 --format="%G?%n%GT%n%GK%n%GS%n%GF%n%GP" seventh-unsigned >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'log.showsignature behaves like --show-signature' '
	test_config log.showsignature true &&
	git show initial >actual &&
	grep "gpg: Signature made" actual &&
	grep "gpg: Good signature" actual
'

test_expect_success GPG 'check config gpg.format values' '
	test_config gpg.format openpgp &&
	git commit -S --amend -m "success" &&
	test_config gpg.format OpEnPgP &&
	test_must_fail git commit -S --amend -m "fail"
'

test_expect_success GPG 'detect fudged commit with double signature' '
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

test_expect_success GPG 'show double signature with custom format' '
	cat >expect <<-\EOF &&
	E




	EOF
	git log -1 --format="%G?%n%GK%n%GS%n%GF%n%GP" $(cat double-commit.commit) >actual &&
	test_cmp expect actual
'


# NEEDSWORK: This test relies on the test_tick commit/author dates from the first
# 'create signed commits' test even though it creates its own
test_expect_success GPG 'verify-commit verifies multiply signed commits' '
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

test_expect_success 'custom `gpg.program`' '
	write_script fake-gpg <<-\EOF &&
	args="$*"

	# skip uninteresting options
	while case "$1" in
	--status-fd=*|--keyid-format=*) ;; # skip
	*) break;;
	esac; do shift; done

	case "$1" in
	-bsau)
		test -z "$LET_GPG_PROGRAM_FAIL" || {
			echo "zOMG signing failed!" >&2
			exit 1
		}
		cat >sign.file
		echo "[GNUPG:] SIG_CREATED $args" >&2
		echo "-----BEGIN PGP MESSAGE-----"
		echo "$args"
		echo "-----END PGP MESSAGE-----"
		;;
	--verify)
		cat "$2" >verify.file
		exit 0
		;;
	*)
		echo "Unhandled args: $*" >&2
		exit 1
		;;
	esac
	EOF

	test_config gpg.program "$(pwd)/fake-gpg" &&
	git commit -S --allow-empty -m signed-commit &&
	test_path_exists sign.file &&
	git show --show-signature &&
	test_path_exists verify.file &&

	test_must_fail env LET_GPG_PROGRAM_FAIL=1 \
	git commit -S --allow-empty -m must-fail 2>err &&
	grep zOMG err
'

test_done
