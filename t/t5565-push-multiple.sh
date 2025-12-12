#!/bin/sh

test_description='push to group'

. ./test-lib.sh

test_expect_success setup '
	for i in 1 2 3
	do
		git init dest-$i &&
		git -C dest-$i symbolic-ref HEAD refs/heads/not-a-branch ||
		return 1
	done &&
	test_tick &&
	git commit --allow-empty -m "initial" &&
	git config set --append remote.them.pushurl "file://$(pwd)/dest-1" &&
	git config set --append remote.them.pushurl "file://$(pwd)/dest-2" &&
	git config set --append remote.them.pushurl "file://$(pwd)/dest-3" &&
	git config set --append remote.them.push "+refs/heads/*:refs/heads/*"
'

test_expect_success 'push to group' '
	git push them &&
	j= &&
	for i in 1 2 3
	do
		git -C dest-$i for-each-ref >actual-$i &&
		if test -n "$j"
		then
			test_cmp actual-$j actual-$i
		else
			cat actual-$i
		fi &&
		j=$i ||
		return 1
	done
'

test_done
