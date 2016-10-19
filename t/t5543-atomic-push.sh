#!/bin/sh

test_description='pushing to a mirror repository'

. ./test-lib.sh

D=`pwd`

invert () {
	if "$@"; then
		return 1
	else
		return 0
	fi
}

mk_repo_pair () {
	rm -rf master mirror &&
	mkdir mirror &&
	(
		cd mirror &&
		git init &&
		git config receive.denyCurrentBranch warn
	) &&
	mkdir master &&
	(
		cd master &&
		git init &&
		git remote add $1 up ../mirror
	)
}


test_expect_success 'atomic push works for a single branch' '

	mk_repo_pair &&
	(
		cd master &&
		echo one >foo && git add foo && git commit -m one &&
		git push --mirror up
		echo two >foo && git add foo && git commit -m two &&
		git push --atomic-push --mirror up
	) &&
	master_master=$(cd master && git show-ref -s --verify refs/heads/master) &&
	mirror_master=$(cd mirror && git show-ref -s --verify refs/heads/master) &&
	test "$master_master" = "$mirror_master"

'

test_expect_success 'atomic push works for two branches' '

	mk_repo_pair &&
	(
		cd master &&
		echo one >foo && git add foo && git commit -m one &&
		git branch second &&
		git push --mirror up
		echo two >foo && git add foo && git commit -m two &&
		git checkout second &&
		echo three >foo && git add foo && git commit -m three &&
		git checkout master &&
		git push --atomic-push --mirror up
	) &&
	master_master=$(cd master && git show-ref -s --verify refs/heads/master) &&
	mirror_master=$(cd mirror && git show-ref -s --verify refs/heads/master) &&
	test "$master_master" = "$mirror_master"

	master_second=$(cd master && git show-ref -s --verify refs/heads/second) &&
	mirror_second=$(cd mirror && git show-ref -s --verify refs/heads/second) &&
	test "$master_second" = "$mirror_second"
'

# set up two branches where master can be pushed but second can not
# (non-fast-forward). Since second can not be pushed the whole operation
# will fail and leave master untouched.
test_expect_success 'atomic push fails if one branch fails' '
	mk_repo_pair &&
	(
		cd master &&
		echo one >foo && git add foo && git commit -m one &&
		git branch second &&
		git checkout second &&
		echo two >foo && git add foo && git commit -m two &&
		echo three >foo && git add foo && git commit -m three &&
		echo four >foo && git add foo && git commit -m four &&
		git push --mirror up
		git reset --hard HEAD~2 &&
		git checkout master
		echo five >foo && git add foo && git commit -m five &&
		! git push --atomic-push --all up
	) &&
	master_master=$(cd master && git show-ref -s --verify refs/heads/master) &&
	mirror_master=$(cd mirror && git show-ref -s --verify refs/heads/master) &&
	test "$master_master" != "$mirror_master" &&

	master_second=$(cd master && git show-ref -s --verify refs/heads/second) &&
	mirror_second=$(cd mirror && git show-ref -s --verify refs/heads/second) &&
	test "$master_second" != "$mirror_second"
'

test_done
