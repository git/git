#!/bin/sh

test_description='git pull message generation'

. ./test-lib.sh

dollar='$Dollar'

test_expect_success setup '
	test_commit initial afile original &&
	git clone . cloned &&
	(
		cd cloned &&
		echo added >bfile &&
		git add bfile &&
		test_tick &&
		git commit -m "add bfile"
	) &&
	test_tick && test_tick &&
	echo "second" >afile &&
	git add afile &&
	git commit -m "second commit" &&
	echo "original $dollar" >afile &&
	git add afile &&
	git commit -m "do not clobber $dollar signs"
'

test_expect_success pull '
(
	cd cloned &&
	git pull --no-rebase --log &&
	git log -2 &&
	git cat-file commit HEAD >result &&
	grep Dollar result
)
'

test_expect_success '--log=1 limits shortlog length' '
(
	cd cloned &&
	git reset --hard HEAD^ &&
	test "$(cat afile)" = original &&
	test "$(cat bfile)" = added &&
	git pull --no-rebase --log=1 &&
	git log -3 &&
	git cat-file commit HEAD >result &&
	grep Dollar result &&
	! grep "second commit" result
)
'

test_done
