#!/bin/sh

test_description='signed tag tests'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPGSSH 'create signed tags ssh' '
	test_when_finished "test_unconfig commit.gpgsign" &&
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&

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
	git tag -u"${GPGSSH_KEY_UNTRUSTED}" -m eighth eighth-signed-alt
'

test_expect_success GPGSSH 'verify and show ssh signatures' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	(
		for tag in initial second merge fourth-signed sixth-signed seventh-signed
		do
			git verify-tag $tag 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in fourth-unsigned fifth-unsigned sixth-unsigned
		do
			test_must_fail git verify-tag $tag 2>actual &&
			! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in eighth-signed-alt
		do
			test_must_fail git verify-tag $tag 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			grep "${GPGSSH_KEY_NOT_TRUSTED}" actual &&
			echo $tag OK || exit 1
		done
	)
'

test_expect_success GPGSSH 'detect fudged ssh signature' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git cat-file tag seventh-signed >raw &&
	sed -e "/^tag / s/seventh/7th forged/" raw >forged1 &&
	git hash-object -w -t tag forged1 >forged1.tag &&
	test_must_fail git verify-tag $(cat forged1.tag) 2>actual1 &&
	grep "${GPGSSH_BAD_SIGNATURE}" actual1 &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual1 &&
	! grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual1
'

test_expect_success GPGSSH 'verify ssh signatures with --raw' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	(
		for tag in initial second merge fourth-signed sixth-signed seventh-signed
		do
			git verify-tag --raw $tag 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in fourth-unsigned fifth-unsigned sixth-unsigned
		do
			test_must_fail git verify-tag --raw $tag 2>actual &&
			! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	) &&
	(
		for tag in eighth-signed-alt
		do
			test_must_fail git verify-tag --raw $tag 2>actual &&
			grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual &&
			! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
			echo $tag OK || exit 1
		done
	)
'

test_expect_success GPGSSH 'verify signatures with --raw ssh' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git verify-tag --raw sixth-signed 2>actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
	echo sixth-signed OK
'

test_expect_success GPGSSH 'verify multiple tags ssh' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	tags="seventh-signed sixth-signed" &&
	for i in $tags
	do
		git verify-tag -v --raw $i || return 1
	done >expect.stdout 2>expect.stderr.1 &&
	grep "^${GPGSSH_GOOD_SIGNATURE_TRUSTED}" <expect.stderr.1 >expect.stderr &&
	git verify-tag -v --raw $tags >actual.stdout 2>actual.stderr.1 &&
	grep "^${GPGSSH_GOOD_SIGNATURE_TRUSTED}" <actual.stderr.1 >actual.stderr &&
	test_cmp expect.stdout actual.stdout &&
	test_cmp expect.stderr actual.stderr
'

test_expect_success GPGSSH 'verifying tag with --format - ssh' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	cat >expect <<-\EOF &&
	tagname : fourth-signed
	EOF
	git verify-tag --format="tagname : %(tag)" "fourth-signed" >actual &&
	test_cmp expect actual
'

test_expect_success GPGSSH 'verifying a forged tag with --format should fail silently - ssh' '
	test_must_fail git verify-tag --format="tagname : %(tag)" $(cat forged1.tag) >actual-forged &&
	test_must_be_empty actual-forged
'

test_done
