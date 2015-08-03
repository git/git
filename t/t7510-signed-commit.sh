#!/bin/sh

test_description='signed commit tests'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPG 'create signed commits' '
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

	git checkout master &&
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
	git tag eighth-signed-alt
'

test_expect_success GPG 'verify and show signatures' '
	(
		for commit in initial second merge fourth-signed fifth-signed sixth-signed seventh-signed
		do
			git verify-commit $commit &&
			git show --pretty=short --show-signature $commit >actual &&
			grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in merge^2 fourth-unsigned sixth-unsigned seventh-unsigned
		do
			test_must_fail git verify-commit $commit &&
			git show --pretty=short --show-signature $commit >actual &&
			! grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			echo $commit OK || exit 1
		done
	) &&
	(
		for commit in eighth-signed-alt
		do
			git show --pretty=short --show-signature $commit >actual &&
			grep "Good signature from" actual &&
			! grep "BAD signature from" actual &&
			grep "not certified" actual &&
			echo $commit OK || exit 1
		done
	)
'

test_expect_success GPG 'verify-commit exits success on untrusted signature' '
	git verify-commit eighth-signed-alt 2>actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual &&
	grep "not certified" actual
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

test_expect_success GPG 'show signed commit with signature' '
	git show -s initial >commit &&
	git show -s --show-signature initial >show &&
	git verify-commit -v initial >verify.1 2>verify.2 &&
	git cat-file commit initial >cat &&
	grep -v -e "gpg: " -e "Warning: " show >show.commit &&
	grep -e "gpg: " -e "Warning: " show >show.gpg &&
	grep -v "^ " cat | grep -v "^gpgsig " >cat.commit &&
	test_cmp show.commit commit &&
	test_cmp show.gpg verify.2 &&
	test_cmp cat.commit verify.1
'

test_expect_success GPG 'detect fudged signature' '
	git cat-file commit seventh-signed >raw &&

	sed -e "s/seventh/7th forged/" raw >forged1 &&
	git hash-object -w -t commit forged1 >forged1.commit &&
	! git verify-commit $(cat forged1.commit) &&
	git show --pretty=short --show-signature $(cat forged1.commit) >actual1 &&
	grep "BAD signature from" actual1 &&
	! grep "Good signature from" actual1
'

test_expect_success GPG 'detect fudged signature with NUL' '
	git cat-file commit seventh-signed >raw &&
	cat raw >forged2 &&
	echo Qwik | tr "Q" "\000" >>forged2 &&
	git hash-object -w -t commit forged2 >forged2.commit &&
	! git verify-commit $(cat forged2.commit) &&
	git show --pretty=short --show-signature $(cat forged2.commit) >actual2 &&
	grep "BAD signature from" actual2 &&
	! grep "Good signature from" actual2
'

test_expect_success GPG 'amending already signed commit' '
	git checkout fourth-signed^0 &&
	git commit --amend -S --no-edit &&
	git verify-commit HEAD &&
	git show -s --show-signature HEAD >actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual
'

test_expect_success GPG 'show good signature with custom format' '
	cat >expect <<-\EOF &&
	G
	13B6F51ECDDE430D
	C O Mitter <committer@example.com>
	EOF
	git log -1 --format="%G?%n%GK%n%GS" sixth-signed >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show bad signature with custom format' '
	cat >expect <<-\EOF &&
	B
	13B6F51ECDDE430D
	C O Mitter <committer@example.com>
	EOF
	git log -1 --format="%G?%n%GK%n%GS" $(cat forged1.commit) >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show unknown signature with custom format' '
	cat >expect <<-\EOF &&
	U
	61092E85B7227189
	Eris Discordia <discord@example.net>
	EOF
	git log -1 --format="%G?%n%GK%n%GS" eighth-signed-alt >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'show lack of signature with custom format' '
	cat >expect <<-\EOF &&
	N


	EOF
	git log -1 --format="%G?%n%GK%n%GS" seventh-unsigned >actual &&
	test_cmp expect actual
'

test_done
