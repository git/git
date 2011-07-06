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
	echo "original $dollar" >afile &&
	git add afile &&
	git commit -m "do not clobber $dollar signs"
'

test_expect_success pull '
(
	cd cloned &&
	git pull --log &&
	git log -2 &&
	git cat-file commit HEAD >result &&
	grep Dollar result
)
'

test_done
