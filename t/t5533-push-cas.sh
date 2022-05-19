#!/bin/sh

test_description='compare & swap push force/delete safety'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

setup_srcdst_basic () {
	rm -fr src dst &&
	but clone --no-local . src &&
	but clone --no-local src dst &&
	(
		cd src && but checkout HEAD^0
	)
}

# For tests with "--force-if-includes".
setup_src_dup_dst () {
	rm -fr src dup dst &&
	but init --bare dst &&
	but clone --no-local dst src &&
	but clone --no-local dst dup
	(
		cd src &&
		test_cummit A &&
		test_cummit B &&
		test_cummit C &&
		but push origin
	) &&
	(
		cd dup &&
		but fetch &&
		but merge origin/main &&
		but switch -c branch main~2 &&
		test_cummit D &&
		test_cummit E &&
		but push origin --all
	) &&
	(
		cd src &&
		but switch main &&
		but fetch --all &&
		but branch branch --track origin/branch &&
		but rebase origin/main
	) &&
	(
		cd dup &&
		but switch main &&
		test_cummit F &&
		test_cummit G &&
		but switch branch &&
		test_commit H &&
		but push origin --all
	)
}

test_expect_success setup '
	# create template repository
	test_cummit A &&
	test_cummit B &&
	test_cummit C
'

test_expect_success 'push to update (protected)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		test_cummit D &&
		test_must_fail but push --force-with-lease=main:main origin main 2>err &&
		grep "stale info" err
	) &&
	but ls-remote . refs/heads/main >expect &&
	but ls-remote src refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (protected, forced)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		test_cummit D &&
		but push --force --force-with-lease=main:main origin main 2>err &&
		grep "forced update" err
	) &&
	but ls-remote dst refs/heads/main >expect &&
	but ls-remote src refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (protected, tracking)' '
	setup_srcdst_basic &&
	(
		cd src &&
		but checkout main &&
		test_cummit D &&
		but checkout HEAD^0
	) &&
	but ls-remote src refs/heads/main >expect &&
	(
		cd dst &&
		test_cummit E &&
		but ls-remote . refs/remotes/origin/main >expect &&
		test_must_fail but push --force-with-lease=main origin main &&
		but ls-remote . refs/remotes/origin/main >actual &&
		test_cmp expect actual
	) &&
	but ls-remote src refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (protected, tracking, forced)' '
	setup_srcdst_basic &&
	(
		cd src &&
		but checkout main &&
		test_cummit D &&
		but checkout HEAD^0
	) &&
	(
		cd dst &&
		test_cummit E &&
		but ls-remote . refs/remotes/origin/main >expect &&
		but push --force --force-with-lease=main origin main
	) &&
	but ls-remote dst refs/heads/main >expect &&
	but ls-remote src refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (allowed)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		test_cummit D &&
		but push --force-with-lease=main:main^ origin main
	) &&
	but ls-remote dst refs/heads/main >expect &&
	but ls-remote src refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (allowed, tracking)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		test_cummit D &&
		but push --force-with-lease=main origin main 2>err &&
		! grep "forced update" err
	) &&
	but ls-remote dst refs/heads/main >expect &&
	but ls-remote src refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'push to update (allowed even though no-ff)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		but reset --hard HEAD^ &&
		test_cummit D &&
		but push --force-with-lease=main origin main 2>err &&
		grep "forced update" err
	) &&
	but ls-remote dst refs/heads/main >expect &&
	but ls-remote src refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'push to delete (protected)' '
	setup_srcdst_basic &&
	but ls-remote src refs/heads/main >expect &&
	(
		cd dst &&
		test_must_fail but push --force-with-lease=main:main^ origin :main
	) &&
	but ls-remote src refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'push to delete (protected, forced)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		but push --force --force-with-lease=main:main^ origin :main
	) &&
	but ls-remote src refs/heads/main >actual &&
	test_must_be_empty actual
'

test_expect_success 'push to delete (allowed)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		but push --force-with-lease=main origin :main 2>err &&
		grep deleted err
	) &&
	but ls-remote src refs/heads/main >actual &&
	test_must_be_empty actual
'

test_expect_success 'cover everything with default force-with-lease (protected)' '
	setup_srcdst_basic &&
	(
		cd src &&
		but branch nain main^
	) &&
	but ls-remote src refs/heads/\* >expect &&
	(
		cd dst &&
		test_must_fail but push --force-with-lease origin main main:nain
	) &&
	but ls-remote src refs/heads/\* >actual &&
	test_cmp expect actual
'

test_expect_success 'cover everything with default force-with-lease (allowed)' '
	setup_srcdst_basic &&
	(
		cd src &&
		but branch nain main^
	) &&
	(
		cd dst &&
		but fetch &&
		but push --force-with-lease origin main main:nain
	) &&
	but ls-remote dst refs/heads/main |
	sed -e "s/main/nain/" >expect &&
	but ls-remote src refs/heads/nain >actual &&
	test_cmp expect actual
'

test_expect_success 'new branch covered by force-with-lease' '
	setup_srcdst_basic &&
	(
		cd dst &&
		but branch branch main &&
		but push --force-with-lease=branch origin branch
	) &&
	but ls-remote dst refs/heads/branch >expect &&
	but ls-remote src refs/heads/branch >actual &&
	test_cmp expect actual
'

test_expect_success 'new branch covered by force-with-lease (explicit)' '
	setup_srcdst_basic &&
	(
		cd dst &&
		but branch branch main &&
		but push --force-with-lease=branch: origin branch
	) &&
	but ls-remote dst refs/heads/branch >expect &&
	but ls-remote src refs/heads/branch >actual &&
	test_cmp expect actual
'

test_expect_success 'new branch already exists' '
	setup_srcdst_basic &&
	(
		cd src &&
		but checkout -b branch main &&
		test_cummit F
	) &&
	(
		cd dst &&
		but branch branch main &&
		test_must_fail but push --force-with-lease=branch: origin branch
	)
'

test_expect_success 'background updates of REMOTE can be mitigated with a non-updated REMOTE-push' '
	rm -rf src dst &&
	but init --bare src.bare &&
	test_when_finished "rm -rf src.bare" &&
	but clone --no-local src.bare dst &&
	test_when_finished "rm -rf dst" &&
	(
		cd dst &&
		test_cummit G &&
		but remote add origin-push ../src.bare &&
		but push origin-push main:main
	) &&
	but clone --no-local src.bare dst2 &&
	test_when_finished "rm -rf dst2" &&
	(
		cd dst2 &&
		test_commit H &&
		but push
	) &&
	(
		cd dst &&
		test_cummit I &&
		but fetch origin &&
		test_must_fail but push --force-with-lease origin-push &&
		but fetch origin-push &&
		but push --force-with-lease origin-push
	)
'

test_expect_success 'background updates to remote can be mitigated with "--force-if-includes"' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	but ls-remote dst refs/heads/main >expect.main &&
	but ls-remote dst refs/heads/branch >expect.branch &&
	(
		cd src &&
		but switch branch &&
		test_cummit I &&
		but switch main &&
		test_cummit J &&
		but fetch --all &&
		test_must_fail but push --force-with-lease --force-if-includes --all
	) &&
	but ls-remote dst refs/heads/main >actual.main &&
	but ls-remote dst refs/heads/branch >actual.branch &&
	test_cmp expect.main actual.main &&
	test_cmp expect.branch actual.branch
'

test_expect_success 'background updates to remote can be mitigated with "push.useForceIfIncludes"' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	but ls-remote dst refs/heads/main >expect.main &&
	(
		cd src &&
		but switch branch &&
		test_cummit I &&
		but switch main &&
		test_cummit J &&
		but fetch --all &&
		but config --local push.useForceIfIncludes true &&
		test_must_fail but push --force-with-lease=main origin main
	) &&
	but ls-remote dst refs/heads/main >actual.main &&
	test_cmp expect.main actual.main
'

test_expect_success '"--force-if-includes" should be disabled for --force-with-lease="<refname>:<expect>"' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	but ls-remote dst refs/heads/main >expect.main &&
	(
		cd src &&
		but switch branch &&
		test_cummit I &&
		but switch main &&
		test_cummit J &&
		remote_head="$(but rev-parse refs/remotes/origin/main)" &&
		but fetch --all &&
		test_must_fail but push --force-if-includes --force-with-lease="main:$remote_head" 2>err &&
		grep "stale info" err
	) &&
	but ls-remote dst refs/heads/main >actual.main &&
	test_cmp expect.main actual.main
'

test_expect_success '"--force-if-includes" should allow forced update after a rebase ("pull --rebase")' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	(
		cd src &&
		but switch branch &&
		test_cummit I &&
		but switch main &&
		test_cummit J &&
		but pull --rebase origin main &&
		but push --force-if-includes --force-with-lease="main"
	)
'

test_expect_success '"--force-if-includes" should allow forced update after a rebase ("pull --rebase", local rebase)' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	(
		cd src &&
		but switch branch &&
		test_cummit I &&
		but switch main &&
		test_cummit J &&
		but pull --rebase origin main &&
		but rebase --onto HEAD~4 HEAD~1 &&
		but push --force-if-includes --force-with-lease="main"
	)
'

test_expect_success '"--force-if-includes" should allow deletes' '
	setup_src_dup_dst &&
	test_when_finished "rm -fr dst src dup" &&
	(
		cd src &&
		but switch branch &&
		but pull --rebase origin branch &&
		but push --force-if-includes --force-with-lease="branch" origin :branch
	)
'

test_done
