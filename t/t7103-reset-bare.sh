#!/bin/sh

test_description='git-reset in a bare repository'
. ./test-lib.sh

test_expect_success 'setup non-bare' '
	echo one >file &&
	git add file &&
	git commit -m one &&
	echo two >file &&
	git commit -a -m two
'

test_expect_success 'setup bare' '
	git clone --bare . bare.git &&
	cd bare.git
'

test_expect_success 'hard reset is not allowed' '
	! git reset --hard HEAD^
'

test_expect_success 'soft reset is allowed' '
	git reset --soft HEAD^ &&
	test "`git show --pretty=format:%s | head -n 1`" = "one"
'

test_done
