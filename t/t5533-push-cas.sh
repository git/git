#!/bin/sh

test_description='compare & swap push force/delete safety'

. ./test-lib.sh

setup_srcdst_basic () {
	rm -fr src dst &&
	git clone --no-local . src &&
	git clone --no-local src dst &&
	(
		cd src && git checkout HEAD^0
	)
}

# For tests with "--force-if-includes".
setup_src_dup_dst () {
	rm -fr src dup dst &&
	git init --bare dst &&
	git clone --no-local dst src &&
	git clone --no-local dst dup
	(
		cd src &&
		test_commit A &&
		test_commit B &&
		test_commit C &&
		git push origin
	) &&
	(
		cd dup &&
		git fetch &&
		git merge origin/master &&
		git switch -c branch master~2 &&
		test_commit D &&
		test_commit E &&
		git push origin --all
	) &&
	(
		cd src &&
		git switch master &&
		git fetch --all &&
		git branch branch --track origin/branch &&
		git rebase origin/master
	) &&
	(
		cd dup &&
		git switch master &&
		test_commit F &&
		test_commit G &&
		git switch branch &&
		test_commit H &&
		git push origin --all
	)
}

test_expect_success setup '
	# create template repository
	test_commit A &&
	test_commit B &&
	test_commit C
'

test_expect_success 'push to update (protected)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		test_commit D &&
		test_must_fail git push --force-with-lease=master:master origin master 2>err &&
		grep "stale info" err
	) &&
	git ls-remote . refs/heads/master >expect &&
	git ls-remote src refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (protected, forced)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		test_commit D &&
		git push --force --force-with-lease=master:master origin master 2>err &&
		grep "forced update" err
	) &&
	git ls-remote dst refs/heads/master >expect &&
	git ls-remote src refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (protected, tracking)' '
	setup_srcdst_basic &&
	(
		cd src &&
		git checkout master &&
		test_commit D &&
		git checkout HEAD^0
	) &&
	git ls-remote src refs/heads/master >expect &&
	(
		cd dst &&
		test_commit E &&
		git ls-remote . refs/remotes/origin/master >expect &&
		test_must_fail git push --force-with-lease=master origin master &&
		git ls-remote . refs/remotes/origin/master >actual &&
		test_cmp expect actual
	) &&
	git ls-remote src refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (protected, tracking, forced)' '
	setup_srcdst_basic &&
	(
		cd src &&
		git checkout master &&
		test_commit D &&
		git checkout HEAD^0
	) &&
	(
		cd dst &&
		test_commit E &&
		git ls-remote . refs/remotes/origin/master >expect &&
		git push --force --force-with-lease=master origin master
	) &&
	git ls-remote dst refs/heads/master >expect &&
	git ls-remote src refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (allowed)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		test_commit D &&
		git push --force-with-lease=master:master^ origin master
	) &&
	git ls-remote dst refs/heads/master >expect &&
	git ls-remote src refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (allowed, tracking)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		test_commit D &&
		git push --force-with-lease=master origin master 2>err &&
		! grep "forced update" err
	) &&
	git ls-remote dst refs/heads/master >expect &&
	git ls-remote src refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (allowed even though no-ff)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		git reset --hard HEAD^ &&
		test_commit D &&
		git push --force-with-lease=master origin master 2>err &&
		grep "forced update" err
	) &&
	git ls-remote dst refs/heads/master >expect &&
	git ls-remote src refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'push to delete (protected)' '
	setup_srcdst_basic &&
	git ls-remote src refs/heads/master >expect &&
	(
		cd dst &&
		test_must_fail git push --force-with-lease=master:master^ origin :master
	) &&
	git ls-remote src refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'push to delete (protected, forced)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		git push --force --force-with-lease=master:master^ origin :master
	) &&
	git ls-remote src refs/heads/master >actual &&
	test_must_be_empty actual
'

test_expect_success 'push to delete (allowed)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		git push --force-with-lease=master origin :master 2>err &&
		grep deleted err
	) &&
	git ls-remote src refs/heads/master >actual &&
	test_must_be_empty actual
'

test_expect_success 'cover everything with default force-with-lease (protected)' '
	setup_srcdst_basic &&
	(
		cd src &&
		git branch naster master^
	) &&
	git ls-remote src refs/heads/\* >expect &&
	(
		cd dst &&
		test_must_fail git push --force-with-lease origin master master:naster
	) &&
	git ls-remote src refs/heads/\* >actual &&
	test_cmp expect actual
'

test_expect_success 'cover everything with default force-with-lease (allowed)' '
	setup_srcdst_basic &&
	(
		cd src &&
		git branch naster master^
	) &&
	(
		cd dst &&
		git fetch &&
		git push --force-with-lease origin master master:naster
	) &&
	git ls-remote dst refs/heads/master |
	sed -e "s/master/naster/" >expect &&
	git ls-remote src refs/heads/naster >actual &&
	test_cmp expect actual
'

test_expect_success 'new branch covered by force-with-lease' '
	setup_srcdst_basic &&
	(
		cd dst &&
		git branch branch master &&
		git push --force-with-lease=branch origin branch
	) &&
	git ls-remote dst refs/heads/branch >expect &&
	git ls-remote src refs/heads/branch >actual &&
	test_cmp expect actual
'

test_expect_success 'new branch covered by force-with-lease (explicit)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		git branch branch master &&
		git push --force-with-lease=branch: origin branch
	) &&
	git ls-remote dst refs/heads/branch >expect &&
	git ls-remote src refs/heads/branch >actual &&
	test_cmp expect actual
'

test_expect_success 'new branch already exists' '
	setup_srcdst_basic &&
	(
		cd src &&
		git checkout -b branch master &&
		test_commit F
	) &&
	(
		cd dst &&
		git branch branch master &&
		test_must_fail git push --force-with-lease=branch: origin branch
	)
'

test_expect_success 'background updates of REMOTE can be mitigated with a non-updated REMOTE-push' '
	rm -rf src dst &&
	git init --bare src.bare &&
	test_when_finished "rm -rf src.bare" &&
	git clone --no-local src.bare dst &&
	test_when_finished "rm -rf dst" &&
	(
		cd dst &&
		test_commit G &&
		git remote add origin-push ../src.bare &&
		git push origin-push master:master
	) &&
	git clone --no-local src.bare dst2 &&
	test_when_finished "rm -rf dst2" &&
	(
		cd dst2 &&
		test_commit H &&
		git push
	) &&
	(
		cd dst &&
		test_commit I &&
		git fetch origin &&
		test_must_fail git push --force-with-lease origin-push &&
		git fetch origin-push &&
		git push --force-with-lease origin-push
	)
'

test_expect_success 'background updates to remote can be mitigated with "--force-if-includes"' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	git ls-remote dst refs/heads/master >expect.master &&
	git ls-remote dst refs/heads/branch >expect.branch &&
	(
		cd src &&
		git switch branch &&
		test_commit I &&
		git switch master &&
		test_commit J &&
		git fetch --all &&
		test_must_fail git push --force-with-lease --force-if-includes --all
	) &&
	git ls-remote dst refs/heads/master >actual.master &&
	git ls-remote dst refs/heads/branch >actual.branch &&
	test_cmp expect.master actual.master &&
	test_cmp expect.branch actual.branch
'

test_expect_success 'background updates to remote can be mitigated with "push.useForceIfIncludes"' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	git ls-remote dst refs/heads/master >expect.master &&
	(
		cd src &&
		git switch branch &&
		test_commit I &&
		git switch master &&
		test_commit J &&
		git fetch --all &&
		git config --local push.useForceIfIncludes true &&
		test_must_fail git push --force-with-lease=master origin master
	) &&
	git ls-remote dst refs/heads/master >actual.master &&
	test_cmp expect.master actual.master
'

test_expect_success '"--force-if-includes" should be disabled for --force-with-lease="<refname>:<expect>"' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	git ls-remote dst refs/heads/master >expect.master &&
	(
		cd src &&
		git switch branch &&
		test_commit I &&
		git switch master &&
		test_commit J &&
		remote_head="$(git rev-parse refs/remotes/origin/master)" &&
		git fetch --all &&
		test_must_fail git push --force-if-includes --force-with-lease="master:$remote_head" 2>err &&
		grep "stale info" err
	) &&
	git ls-remote dst refs/heads/master >actual.master &&
	test_cmp expect.master actual.master
'

test_expect_success '"--force-if-includes" should allow forced update after a rebase ("pull --rebase")' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	(
		cd src &&
		git switch branch &&
		test_commit I &&
		git switch master &&
		test_commit J &&
		git pull --rebase origin master &&
		git push --force-if-includes --force-with-lease="master"
	)
'

test_expect_success '"--force-if-includes" should allow forced update after a rebase ("pull --rebase", local rebase)' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	(
		cd src &&
		git switch branch &&
		test_commit I &&
		git switch master &&
		test_commit J &&
		git pull --rebase origin master &&
		git rebase --onto HEAD~4 HEAD~1 &&
		git push --force-if-includes --force-with-lease="master"
	)
'

test_expect_success '"--force-if-includes" should allow deletes' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	(
		cd src &&
		git switch branch &&
		git pull --rebase origin branch &&
		git push --force-if-includes --force-with-lease="branch" origin :branch
	)
'

test_done
