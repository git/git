#!/bin/sh

test_description='but pull message generation'

. ./test-lib.sh

dollar='$Dollar'

test_expect_success setup '
	test_cummit initial afile original &&
	but clone . cloned &&
	(
		cd cloned &&
		echo added >bfile &&
		but add bfile &&
		test_tick &&
		but cummit -m "add bfile"
	) &&
	test_tick && test_tick &&
	echo "second" >afile &&
	but add afile &&
	but cummit -m "second cummit" &&
	echo "original $dollar" >afile &&
	but add afile &&
	but cummit -m "do not clobber $dollar signs"
'

test_expect_success pull '
(
	cd cloned &&
	but pull --no-rebase --log &&
	but log -2 &&
	but cat-file commit HEAD >result &&
	grep Dollar result
)
'

test_expect_success '--log=1 limits shortlog length' '
(
	cd cloned &&
	but reset --hard HEAD^ &&
	test "$(cat afile)" = original &&
	test "$(cat bfile)" = added &&
	but pull --no-rebase --log=1 &&
	but log -3 &&
	but cat-file commit HEAD >result &&
	grep Dollar result &&
	! grep "second cummit" result
)
'

test_done
