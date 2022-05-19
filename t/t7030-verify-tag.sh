#!/bin/sh

test_description='signed tag tests'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPG 'create signed tags' '
	echo 1 >file && but add file &&
	test_tick && but cummit -m initial &&
	but tag -s -m initial initial &&
	but branch side &&

	echo 2 >file && test_tick && but cummit -a -m second &&
	but tag -s -m second second &&

	but checkout side &&
	echo 3 >elif && but add elif &&
	test_tick && but cummit -m "third on side" &&

	but checkout main &&
	test_tick && but merge -S side &&
	but tag -s -m merge merge &&

	echo 4 >file && test_tick && but cummit -a -S -m "fourth unsigned" &&
	but tag -a -m fourth-unsigned fourth-unsigned &&

	test_tick && but cummit --amend -S -m "fourth signed" &&
	but tag -s -m fourth fourth-signed &&

	echo 5 >file && test_tick && but cummit -a -m "fifth" &&
	but tag fifth-unsigned &&

	but config cummit.gpgsign true &&
	echo 6 >file && test_tick && but cummit -a -m "sixth" &&
	but tag -a -m sixth sixth-unsigned &&

	test_tick && but rebase -f HEAD^^ && but tag -s -m 6th sixth-signed HEAD^ &&
	but tag -m seventh -s seventh-signed &&

	echo 8 >file && test_tick && but cummit -a -m eighth &&
	but tag -uB7227189 -m eighth eighth-signed-alt
'

test_expect_success GPGSM 'create signed tags x509 ' '
	test_config gpg.format x509 &&
	test_config user.signingkey $BUT_CUMMITTER_EMAIL &&
	echo 9 >file && test_tick && but cummit -a -m "ninth gpgsm-signed" &&
	but tag -s -m ninth ninth-signed-x509
'

test_expect_success GPG 'verify and show signatures' '
	(
		for tag in initial second merge fourth-signed sixth-signed seventh-signed
		do
			but verify-tag $tag 2>actual &&
			grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in fourth-unsigned fifth-unsigned sixth-unsigned
		do
			test_must_fail but verify-tag $tag 2>actual &&
			! grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in eighth-signed-alt
		do
			but verify-tag $tag 2>actual &&
			grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			grep "not certified" actual &&
			echo $tag OK || exit 1
		done
	)
'

test_expect_success GPGSM 'verify and show signatures x509' '
	but verify-tag ninth-signed-x509 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPGSM 'verify and show signatures x509 with low minTrustLevel' '
	test_config gpg.minTrustLevel undefined &&
	but verify-tag ninth-signed-x509 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPGSM 'verify and show signatures x509 with matching minTrustLevel' '
	test_config gpg.minTrustLevel fully &&
	but verify-tag ninth-signed-x509 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPGSM 'verify and show signatures x509 with high minTrustLevel' '
	test_config gpg.minTrustLevel ultimate &&
	test_must_fail but verify-tag ninth-signed-x509 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPG 'detect fudged signature' '
	but cat-file tag seventh-signed >raw &&
	sed -e "/^tag / s/seventh/7th forged/" raw >forged1 &&
	but hash-object -w -t tag forged1 >forged1.tag &&
	test_must_fail but verify-tag $(cat forged1.tag) 2>actual1 &&
	grep "BAD signature from" actual1 &&
	! grep "Good signature from" actual1
'

test_expect_success GPG 'verify signatures with --raw' '
	(
		for tag in initial second merge fourth-signed sixth-signed seventh-signed
		do
			but verify-tag --raw $tag 2>actual &&
			grep "GOODSIG" actual &&
			! grep "BADSIG" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in fourth-unsigned fifth-unsigned sixth-unsigned
		do
			test_must_fail but verify-tag --raw $tag 2>actual &&
			! grep "GOODSIG" actual &&
			! grep "BADSIG" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in eighth-signed-alt
		do
			but verify-tag --raw $tag 2>actual &&
			grep "GOODSIG" actual &&
			! grep "BADSIG" actual &&
			grep "TRUST_UNDEFINED" actual &&
			echo $tag OK || exit 1
		done
	)
'

test_expect_success GPGSM 'verify signatures with --raw x509' '
	but verify-tag --raw ninth-signed-x509 2>actual &&
	grep "GOODSIG" actual &&
	! grep "BADSIG" actual &&
	echo ninth-signed-x509 OK
'

test_expect_success GPG 'verify multiple tags' '
	tags="fourth-signed sixth-signed seventh-signed" &&
	for i in $tags
	do
		but verify-tag -v --raw $i || return 1
	done >expect.stdout 2>expect.stderr.1 &&
	grep "^.GNUPG:." <expect.stderr.1 >expect.stderr &&
	but verify-tag -v --raw $tags >actual.stdout 2>actual.stderr.1 &&
	grep "^.GNUPG:." <actual.stderr.1 >actual.stderr &&
	test_cmp expect.stdout actual.stdout &&
	test_cmp expect.stderr actual.stderr
'

test_expect_success GPGSM 'verify multiple tags x509' '
	tags="seventh-signed ninth-signed-x509" &&
	for i in $tags
	do
		but verify-tag -v --raw $i || return 1
	done >expect.stdout 2>expect.stderr.1 &&
	grep "^.GNUPG:." <expect.stderr.1 >expect.stderr &&
	but verify-tag -v --raw $tags >actual.stdout 2>actual.stderr.1 &&
	grep "^.GNUPG:." <actual.stderr.1 >actual.stderr &&
	test_cmp expect.stdout actual.stdout &&
	test_cmp expect.stderr actual.stderr
'

test_expect_success GPG 'verifying tag with --format' '
	cat >expect <<-\EOF &&
	tagname : fourth-signed
	EOF
	but verify-tag --format="tagname : %(tag)" "fourth-signed" >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'verifying tag with --format="%(rest)" must fail' '
	test_must_fail but verify-tag --format="%(rest)" "fourth-signed"
'

test_expect_success GPG 'verifying a forged tag with --format should fail silently' '
	test_must_fail but verify-tag --format="tagname : %(tag)" $(cat forged1.tag) >actual-forged &&
	test_must_be_empty actual-forged
'

test_done
