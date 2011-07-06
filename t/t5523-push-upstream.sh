#!/bin/sh

test_description='push with --set-upstream'
. ./test-lib.sh

test_expect_success 'setup bare parent' '
	git init --bare parent &&
	git remote add upstream parent
'

test_expect_success 'setup local commit' '
	echo content >file &&
	git add file &&
	git commit -m one
'

check_config() {
	(echo $2; echo $3) >expect.$1
	(git config branch.$1.remote
	 git config branch.$1.merge) >actual.$1
	test_cmp expect.$1 actual.$1
}

test_expect_success 'push -u master:master' '
	git push -u upstream master:master &&
	check_config master upstream refs/heads/master
'

test_expect_success 'push -u master:other' '
	git push -u upstream master:other &&
	check_config master upstream refs/heads/other
'

test_expect_success 'push -u --dry-run master:otherX' '
	git push -u --dry-run upstream master:otherX &&
	check_config master upstream refs/heads/other
'

test_expect_success 'push -u master2:master2' '
	git branch master2 &&
	git push -u upstream master2:master2 &&
	check_config master2 upstream refs/heads/master2
'

test_expect_success 'push -u master2:other2' '
	git push -u upstream master2:other2 &&
	check_config master2 upstream refs/heads/other2
'

test_expect_success 'push -u :master2' '
	git push -u upstream :master2 &&
	check_config master2 upstream refs/heads/other2
'

test_expect_success 'push -u --all' '
	git branch all1 &&
	git branch all2 &&
	git push -u --all &&
	check_config all1 upstream refs/heads/all1 &&
	check_config all2 upstream refs/heads/all2
'

test_expect_success 'push -u HEAD' '
	git checkout -b headbranch &&
	git push -u upstream HEAD &&
	check_config headbranch upstream refs/heads/headbranch
'

test_done
