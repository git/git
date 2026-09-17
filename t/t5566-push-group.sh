#!/bin/sh

test_description='push to remote group'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=default
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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

test_expect_success 'push to remote group updates all members correctly' '
	git push all-remotes HEAD:refs/heads/main &&
	git rev-parse HEAD >expect &&
	for i in 1 2 3
	do
		git -C dest-$i.git rev-parse refs/heads/main >actual ||
		return 1
		test_cmp expect actual || return 1
	done
'

test_expect_success 'push second commit to group updates all members' '
	test_tick &&
	git commit --allow-empty -m "second" &&
	git push all-remotes HEAD:refs/heads/main &&
	git rev-parse HEAD >expect &&
	for i in 1 2 3
	do
		git -C dest-$i.git rev-parse refs/heads/main >actual ||
		return 1
		test_cmp expect actual || return 1
	done
'

test_expect_success 'push to single remote in group does not affect others' '
	test_tick &&
	git commit --allow-empty -m "third" &&
	git push remote-1 HEAD:refs/heads/main &&
	git -C dest-1.git rev-parse refs/heads/main >hash-after-1 &&
	git -C dest-2.git rev-parse refs/heads/main >hash-after-2 &&
	! test_cmp hash-after-1 hash-after-2
'

test_expect_success 'mirror remote in group with refspec fails' '
	git config set remote.remote-1.mirror true &&
	test_must_fail git push all-remotes HEAD:refs/heads/main 2>err &&
	test_grep "mirror" err &&
	git config unset remote.remote-1.mirror
'

test_expect_success 'push.default=current works with group push' '
	git config set push.default current &&
	test_tick &&
	git commit --allow-empty -m "fifth" &&
	git push all-remotes &&
	git config unset push.default
'

test_expect_success '--atomic is rejected for group push' '
	test_must_fail git push --atomic all-remotes HEAD:refs/heads/main 2>err &&
	test_grep "atomic" err
'

test_expect_success 'push continues past rejection to remaining remotes' '
	for i in c1 c2 c3
	do
		git init --bare dest-$i.git || return 1
	done &&
	git config set remote.c1.url "file://$(pwd)/dest-c1.git" &&
	git config set remote.c2.url "file://$(pwd)/dest-c2.git" &&
	git config set remote.c3.url "file://$(pwd)/dest-c3.git" &&
	git config set remotes.continue-group "c1 c2 c3" &&

	test_tick &&
	git commit --allow-empty -m "base for continue test" &&

	# initial sync
	git push continue-group HEAD:refs/heads/main &&

	# advance c2 independently
	git clone dest-c2.git tmp-c2 &&
	(
		cd tmp-c2 &&
		git checkout -b main origin/main &&
		test_commit c2_independent &&
		git push origin HEAD:refs/heads/main
	) &&
	rm -rf tmp-c2 &&

	test_tick &&
	git commit --allow-empty -m "local diverging commit" &&

	# push: c2 rejects, others succeed
	test_must_fail git push continue-group HEAD:refs/heads/main &&

	git rev-parse HEAD >expect &&
	git -C dest-c1.git rev-parse refs/heads/main >actual-c1 &&
	git -C dest-c3.git rev-parse refs/heads/main >actual-c3 &&
	test_cmp expect actual-c1 &&
	test_cmp expect actual-c3 &&

	# c2 should not have the new commit
	git -C dest-c2.git rev-parse refs/heads/main >actual-c2 &&
	! test_cmp expect actual-c2
'

test_expect_success 'fatal connection error does not stop remaining remotes' '
	for i in f1 f2 f3
	do
		git init --bare dest-$i.git || return 1
	done &&
	git config set remote.f1.url "file://$(pwd)/dest-f1.git" &&
	git config set remote.f2.url "file://$(pwd)/dest-f2.git" &&
	git config set remote.f3.url "file://$(pwd)/dest-f3.git" &&
	git config set remotes.fatal-group "f1 f2 f3" &&

	test_tick &&
	git commit --allow-empty -m "base for fatal test" &&

	# initial sync
	git push fatal-group HEAD:refs/heads/main &&

	# break f2
	git config set remote.f2.url "file:///tmp/does-not-exist-$$" &&

	test_tick &&
	git commit --allow-empty -m "after fatal setup" &&

	# overall exit code is non-zero because f2 failed
	test_must_fail git push fatal-group HEAD:refs/heads/main &&

	git rev-parse HEAD >expect &&

	# f1 and f3 should both have the new commit — subprocesses are independent
	git -C dest-f1.git rev-parse refs/heads/main >actual-f1 &&
	test_cmp expect actual-f1 &&
	git -C dest-f3.git rev-parse refs/heads/main >actual-f3 &&
	test_cmp expect actual-f3 &&

	git config set remote.f2.url "file://$(pwd)/dest-f2.git"
'

test_done
