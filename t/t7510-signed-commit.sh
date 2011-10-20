#!/bin/sh

test_description='signed commit tests'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPG 'create signed commits' '
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

	test_tick && git commit --amend -S -m "fourth signed"
'

test_expect_success GPG 'show signatures' '
	(
		for commit in initial second merge master
		do
			git show --pretty=short --show-signature $commit >actual &&
			grep "Good signature from" actual || exit 1
			! grep "BAD signature from" actual || exit 1
			echo $commit OK
		done
	) &&
	(
		for commit in merge^2 fourth-unsigned
		do
			git show --pretty=short --show-signature $commit >actual &&
			grep "Good signature from" actual && exit 1
			! grep "BAD signature from" actual || exit 1
			echo $commit OK
		done
	)
'

test_expect_success GPG 'detect fudged signature' '
	git cat-file commit master >raw &&
	sed -e "s/fourth signed/4th forged/" raw >forged &&
	git hash-object -w -t commit forged >forged.commit &&
	git show --pretty=short --show-signature $(cat forged.commit) >actual &&
	grep "BAD signature from" actual &&
	! grep "Good signature from" actual
'

test_done
