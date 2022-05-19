#!/bin/sh

test_description='signed tag tests'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPGSSH 'create signed tags ssh' '
	test_when_finished "test_unconfig cummit.gpgsign" &&
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&

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
	but tag -u"${GPGSSH_KEY_UNTRUSTED}" -m eighth eighth-signed-alt
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'create signed tags with keys having defined lifetimes' '
	test_when_finished "test_unconfig cummit.gpgsign" &&
	test_config gpg.format ssh &&

	echo expired >file && test_tick && but cummit -a -m expired -S"${GPGSSH_KEY_EXPIRED}" &&
	but tag -s -u "${GPGSSH_KEY_EXPIRED}" -m expired-signed expired-signed &&

	echo notyetvalid >file && test_tick && but cummit -a -m notyetvalid -S"${GPGSSH_KEY_NOTYETVALID}" &&
	but tag -s -u "${GPGSSH_KEY_NOTYETVALID}" -m notyetvalid-signed notyetvalid-signed &&

	echo timeboxedvalid >file && test_tick && but cummit -a -m timeboxedvalid -S"${GPGSSH_KEY_TIMEBOXEDVALID}" &&
	but tag -s -u "${GPGSSH_KEY_TIMEBOXEDVALID}" -m timeboxedvalid-signed timeboxedvalid-signed &&

	echo timeboxedinvalid >file && test_tick && but cummit -a -m timeboxedinvalid -S"${GPGSSH_KEY_TIMEBOXEDINVALID}" &&
	but tag -s -u "${GPGSSH_KEY_TIMEBOXEDINVALID}" -m timeboxedinvalid-signed timeboxedinvalid-signed
'

test_expect_success GPGSSH 'verify and show ssh signatures' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	(
		for tag in initial second merge fourth-signed sixth-signed seventh-signed
		do
			but verify-tag $tag 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in fourth-unsigned fifth-unsigned sixth-unsigned
		do
			test_must_fail but verify-tag $tag 2>actual &&
			! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in eighth-signed-alt
		do
			test_must_fail but verify-tag $tag 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			grep "${GPGSSH_KEY_NOT_TRUSTED}" actual &&
			echo $tag OK || exit 1
		done
	)
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'verify-tag exits failure on expired signature key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	test_must_fail but verify-tag expired-signed 2>actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'verify-tag exits failure on not yet valid signature key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	test_must_fail but verify-tag notyetvalid-signed 2>actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'verify-tag succeeds with tag date and key validity matching' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but verify-tag timeboxedvalid-signed 2>actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'verify-tag failes with tag date outside of key validity' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	test_must_fail but verify-tag timeboxedinvalid-signed 2>actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH 'detect fudged ssh signature' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but cat-file tag seventh-signed >raw &&
	sed -e "/^tag / s/seventh/7th forged/" raw >forged1 &&
	but hash-object -w -t tag forged1 >forged1.tag &&
	test_must_fail but verify-tag $(cat forged1.tag) 2>actual1 &&
	grep "${GPGSSH_BAD_SIGNATURE}" actual1 &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual1 &&
	! grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual1
'

test_expect_success GPGSSH 'verify ssh signatures with --raw' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	(
		for tag in initial second merge fourth-signed sixth-signed seventh-signed
		do
			but verify-tag --raw $tag 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in fourth-unsigned fifth-unsigned sixth-unsigned
		do
			test_must_fail but verify-tag --raw $tag 2>actual &&
			! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in eighth-signed-alt
		do
			test_must_fail but verify-tag --raw $tag 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	)
'

test_expect_success GPGSSH 'verify signatures with --raw ssh' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but verify-tag --raw sixth-signed 2>actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
	echo sixth-signed OK
'

test_expect_success GPGSSH 'verify multiple tags ssh' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	tags="seventh-signed sixth-signed" &&
	for i in $tags
	do
		but verify-tag -v --raw $i || return 1
	done >expect.stdout 2>expect.stderr.1 &&
	grep "^${GPGSSH_GOOD_SIGNATURE_TRUSTED}" <expect.stderr.1 >expect.stderr &&
	but verify-tag -v --raw $tags >actual.stdout 2>actual.stderr.1 &&
	grep "^${GPGSSH_GOOD_SIGNATURE_TRUSTED}" <actual.stderr.1 >actual.stderr &&
	test_cmp expect.stdout actual.stdout &&
	test_cmp expect.stderr actual.stderr
'

test_expect_success GPGSSH 'verifying tag with --format - ssh' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	cat >expect <<-\EOF &&
	tagname : fourth-signed
	EOF
	but verify-tag --format="tagname : %(tag)" "fourth-signed" >actual &&
	test_cmp expect actual
'

test_expect_success GPGSSH 'verifying a forged tag with --format should fail silently - ssh' '
	test_must_fail but verify-tag --format="tagname : %(tag)" $(cat forged1.tag) >actual-forged &&
	test_must_be_empty actual-forged
'

test_done
