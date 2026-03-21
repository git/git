#!/bin/sh

test_description='push to remote group'

. ./test-lib.sh

test_expect_success 'setup' '
	for i in 1 2 3
	do
		git init --bare dest-$i.git &&
		git -C dest-$i.git symbolic-ref HEAD refs/heads/not-a-branch ||
		return 1
	done &&
	test_tick &&
	git commit --allow-empty -m "initial" &&
	git config set remote.remote-1.url "file://$(pwd)/dest-1.git" &&
	git config set remote.remote-1.fetch "+refs/heads/*:refs/remotes/remote-1/*" &&
	git config set remote.remote-2.url "file://$(pwd)/dest-2.git" &&
	git config set remote.remote-2.fetch "+refs/heads/*:refs/remotes/remote-2/*" &&
	git config set remote.remote-3.url "file://$(pwd)/dest-3.git" &&
	git config set remote.remote-3.fetch "+refs/heads/*:refs/remotes/remote-3/*" &&
	git config set remotes.all-remotes "remote-1 remote-2 remote-3"
'

test_expect_success 'push to remote group pushes to all members' '
	git push all-remotes HEAD:refs/heads/main &&
	j= &&
	for i in 1 2 3
	do
		git -C dest-$i.git for-each-ref >actual-$i &&
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

test_expect_success 'push second commit to group updates all members' '
	test_tick &&
	git commit --allow-empty -m "second" &&
	git push all-remotes HEAD:refs/heads/main &&
	for i in 1 2 3
	do
		git -C dest-$i.git rev-parse refs/heads/main >hash-$i ||
		return 1
	done &&
	test_cmp hash-1 hash-2 &&
	test_cmp hash-2 hash-3
'

test_expect_success 'push to single remote in group does not affect others' '
	test_tick &&
	git commit --allow-empty -m "third" &&
	git push remote-1 HEAD:refs/heads/main &&
	git -C dest-1.git rev-parse refs/heads/main >hash-after-1 &&
	git -C dest-2.git rev-parse refs/heads/main >hash-after-2 &&
	! test_cmp hash-after-1 hash-after-2
'

test_expect_success 'push to nonexistent group fails with error' '
	test_must_fail git push no-such-group HEAD:refs/heads/main
'

test_expect_success 'push explicit refspec to group' '
	test_tick &&
	git commit --allow-empty -m "fourth" &&
	git push all-remotes HEAD:refs/heads/other &&
	for i in 1 2 3
	do
		git -C dest-$i.git rev-parse refs/heads/other >other-hash-$i ||
		return 1
	done &&
	test_cmp other-hash-1 other-hash-2 &&
	test_cmp other-hash-2 other-hash-3
'

test_expect_success 'mirror remote in group with refspec fails' '
	git config set remote.remote-1.mirror true &&
	test_must_fail git push all-remotes HEAD:refs/heads/main 2>err &&
	grep "mirror" err &&
	git config unset remote.remote-1.mirror
'
test_expect_success 'push.default=current works with group push' '
	git config set push.default current &&
	test_tick &&
	git commit --allow-empty -m "fifth" &&
	git push all-remotes &&
	git config unset push.default
'

test_done
