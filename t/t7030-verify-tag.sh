#!/bin/sh

test_description='signed tag tests'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPG 'create signed tags' '
	echo 1 >file && git add file &&
	test_tick && git commit -m initial &&
	git tag -s -m initial initial &&
	git branch side &&

	echo 2 >file && test_tick && git commit -a -m second &&
	git tag -s -m second second &&

	git checkout side &&
	echo 3 >elif && git add elif &&
	test_tick && git commit -m "third on side" &&

	git checkout main &&
	test_tick && git merge -S side &&
	git tag -s -m merge merge &&

	echo 4 >file && test_tick && git commit -a -S -m "fourth unsigned" &&
	git tag -a -m fourth-unsigned fourth-unsigned &&

	test_tick && git commit --amend -S -m "fourth signed" &&
	git tag -s -m fourth fourth-signed &&

	echo 5 >file && test_tick && git commit -a -m "fifth" &&
	git tag fifth-unsigned &&

	git config commit.gpgsign true &&
	echo 6 >file && test_tick && git commit -a -m "sixth" &&
	git tag -a -m sixth sixth-unsigned &&

	test_tick && git rebase -f HEAD^^ && git tag -s -m 6th sixth-signed HEAD^ &&
	git tag -m seventh -s seventh-signed &&

	echo 8 >file && test_tick && git commit -a -m eighth &&
	git tag -uB7227189 -m eighth eighth-signed-alt
'

test_expect_success GPGSM 'create signed tags x509 ' '
	test_config gpg.format x509 &&
	test_config user.signingkey $GIT_COMMITTER_EMAIL &&
	echo 9 >file && test_tick && git commit -a -m "ninth gpgsm-signed" &&
	git tag -s -m ninth ninth-signed-x509
'

test_expect_success GPG 'verify and show signatures' '
	(
		for tag in initial second merge fourth-signed sixth-signed seventh-signed
		do
			git verify-tag $tag 2>actual &&
			grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in fourth-unsigned fifth-unsigned sixth-unsigned
		do
			test_must_fail git verify-tag $tag 2>actual &&
			! grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in eighth-signed-alt
		do
			git verify-tag $tag 2>actual &&
			grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			grep "not certified" actual &&
			echo $tag OK || exit 1
		done
	)
'

test_expect_success GPGSM 'verify and show signatures x509' '
	git verify-tag ninth-signed-x509 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPGSM 'verify and show signatures x509 with low minTrustLevel' '
	test_config gpg.minTrustLevel undefined &&
	git verify-tag ninth-signed-x509 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPGSM 'verify and show signatures x509 with matching minTrustLevel' '
	test_config gpg.minTrustLevel fully &&
	git verify-tag ninth-signed-x509 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPGSM 'verify and show signatures x509 with high minTrustLevel' '
	test_config gpg.minTrustLevel ultimate &&
	test_must_fail git verify-tag ninth-signed-x509 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPG 'detect fudged signature' '
	git cat-file tag seventh-signed >raw &&
	sed -e "/^tag / s/seventh/7th forged/" raw >forged1 &&
	git hash-object -w -t tag forged1 >forged1.tag &&
	test_must_fail git verify-tag $(cat forged1.tag) 2>actual1 &&
	grep "BAD signature from" actual1 &&
	! grep "Good signature from" actual1
'

test_expect_success GPG 'verify signatures with --raw' '
	(
		for tag in initial second merge fourth-signed sixth-signed seventh-signed
		do
			git verify-tag --raw $tag 2>actual &&
			grep "GOODSIG" actual &&
			! grep "BADSIG" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in fourth-unsigned fifth-unsigned sixth-unsigned
		do
			test_must_fail git verify-tag --raw $tag 2>actual &&
			! grep "GOODSIG" actual &&
			! grep "BADSIG" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in eighth-signed-alt
		do
			git verify-tag --raw $tag 2>actual &&
			grep "GOODSIG" actual &&
			! grep "BADSIG" actual &&
			grep "TRUST_UNDEFINED" actual &&
			echo $tag OK || exit 1
		done
	)
'

test_expect_success GPGSM 'verify signatures with --raw x509' '
	git verify-tag --raw ninth-signed-x509 2>actual &&
	grep "GOODSIG" actual &&
	! grep "BADSIG" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPG 'verify multiple tags' '
	tags="fourth-signed sixth-signed seventh-signed" &&
	for i in $tags
	do
		git verify-tag -v --raw $i || return 1
	done >expect.stdout 2>expect.stderr.1 &&
	grep "^.GNUPG:." <expect.stderr.1 >expect.stderr &&
	git verify-tag -v --raw $tags >actual.stdout 2>actual.stderr.1 &&
	grep "^.GNUPG:." <actual.stderr.1 >actual.stderr &&
	test_cmp expect.stdout actual.stdout &&
	test_cmp expect.stderr actual.stderr
'

test_expect_success GPGSM 'verify multiple tags x509' '
	tags="seventh-signed ninth-signed-x509" &&
	for i in $tags
	do
		git verify-tag -v --raw $i || return 1
	done >expect.stdout 2>expect.stderr.1 &&
	grep "^.GNUPG:." <expect.stderr.1 >expect.stderr &&
	git verify-tag -v --raw $tags >actual.stdout 2>actual.stderr.1 &&
	grep "^.GNUPG:." <actual.stderr.1 >actual.stderr &&
	test_cmp expect.stdout actual.stdout &&
	test_cmp expect.stderr actual.stderr
'

test_expect_success GPG 'verifying tag with --format' '
	cat >expect <<-\EOF &&
	tagname : fourth-signed
	EOF
	git verify-tag --format="tagname : %(tag)" "fourth-signed" >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'verifying tag with --format="%(rest)" must fail' '
	test_must_fail git verify-tag --format="%(rest)" "fourth-signed"
'

test_expect_success GPG 'verifying a forged tag with --format should fail silently' '
	test_must_fail git verify-tag --format="tagname : %(tag)" $(cat forged1.tag) >actual-forged &&
	test_must_be_empty actual-forged
'

test_done
