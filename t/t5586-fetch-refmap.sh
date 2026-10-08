#!/bin/sh

test_description='"git fetch" with remote.<name>.fetchUpstreams

With remote.<name>.fetchUpstreams set, a refspec-less fetch also
fetches the upstreams that local branches have on that remote, and
remote.<name>.fetchRefmap decides where they are stored.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check_upstream_refs () {
	git for-each-ref --format="%(refname)" refs/remotes/upstream >actual &&
	cat >expect &&
	test_cmp expect actual
}

check_upstream_tip () {
	git -C .. rev-parse "$2" >expect &&
	git rev-parse "$1" >actual &&
	test_cmp expect actual
}

test_expect_success 'setup' '
	test_commit main-1 &&
	test_commit main-2 &&
	git checkout -b side main-1 &&
	test_commit side-1 &&
	git checkout -b next main-1 &&
	test_commit next-1 &&
	git checkout main
'

test_expect_success 'clone shallow and single-branch, then add a second remote' '
	git clone --no-local --depth=1 --branch main --single-branch . client &&
	(
		cd client &&
		git remote add upstream .. &&
		test_might_fail git config unset remote.upstream.fetch &&
		git config remote.upstream.fetchRefmap \
			"+refs/heads/*:refs/remotes/upstream/*" &&
		git config remote.upstream.fetchUpstreams true
	)
'

test_expect_success 'a bare fetch needs nothing until a branch is tracked' '
	(
		cd client &&
		git fetch upstream &&
		check_upstream_refs <<-\EOF
		EOF
	)
'

test_expect_success 'an explicit one-time fetch lets a branch be tracked' '
	(
		cd client &&
		git fetch upstream main &&
		git branch --set-upstream-to=upstream/main &&
		test_cmp_config upstream branch.main.remote &&
		test_cmp_config refs/heads/main branch.main.merge
	)
'

test_expect_success 'a branch checked out from a one-time fetch is kept updated by later plain fetches' '
	(
		cd client &&
		git fetch upstream side:refs/remotes/upstream/side &&
		git branch side-topic upstream/side
	) &&
	git checkout side &&
	test_commit side-2 &&
	git checkout main &&
	(
		cd client &&
		git fetch upstream &&
		check_upstream_refs <<-\EOF &&
		refs/remotes/upstream/HEAD
		refs/remotes/upstream/main
		refs/remotes/upstream/side
		EOF
		check_upstream_tip refs/remotes/upstream/side side
	)
'

test_expect_success 'a second branch tracking the same upstream branch does not fetch it twice' '
	(
		cd client &&
		git branch side-topic-2 upstream/side &&
		git fetch upstream &&
		grep side .git/FETCH_HEAD >fetched &&
		test_line_count = 1 fetched
	)
'

test_expect_success 'a branch tracking a different remote is not fetched from upstream' '
	(
		cd client &&
		git remote add other .. &&
		git fetch other next:refs/remotes/other/next &&
		git branch next-topic other/next &&
		git fetch upstream &&
		check_upstream_refs <<-\EOF
		refs/remotes/upstream/HEAD
		refs/remotes/upstream/main
		refs/remotes/upstream/side
		EOF
	)
'

test_expect_success 'upstreams are not fetched without remote.<name>.fetchUpstreams' '
	git checkout side &&
	test_commit side-3 &&
	git checkout main &&
	(
		cd client &&
		git rev-parse refs/remotes/upstream/side >expect &&
		git -c remote.upstream.fetchUpstreams=false fetch upstream &&
		git rev-parse refs/remotes/upstream/side >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'remote.<name>.fetch adds to the upstreams being fetched' '
	(
		cd client &&
		git -c remote.upstream.fetch=next fetch upstream &&
		check_upstream_refs <<-\EOF &&
		refs/remotes/upstream/HEAD
		refs/remotes/upstream/main
		refs/remotes/upstream/next
		refs/remotes/upstream/side
		EOF
		check_upstream_tip refs/remotes/upstream/side side
	)
'

test_expect_success 'prefetch stores tracked branches under refs/prefetch/' '
	test_commit main-3 &&
	(
		cd client &&
		git rev-parse refs/remotes/upstream/main >expect &&
		git fetch --prefetch upstream &&
		git rev-parse refs/remotes/upstream/main >actual &&
		test_cmp expect actual &&
		check_upstream_tip refs/prefetch/remotes/upstream/main main
	)
'

test_expect_success 'git pull merges only the upstream of the current branch' '
	(
		cd client &&
		git pull &&
		check_upstream_tip HEAD main
	)
'

test_expect_success 'git remote show does not choke on a refmap-only remote' '
	(
		cd client &&
		git remote show upstream
	)
'

test_expect_success '@{push} resolves for a branch pushed to a refmap-only remote' '
	(
		cd client &&
		git config push.default current &&
		git checkout -b topic upstream/main &&
		test_commit topic-1 &&
		git push &&
		echo refs/remotes/upstream/topic >expect &&
		git rev-parse --symbolic-full-name topic@{push} >actual &&
		test_cmp expect actual &&
		test_cmp_rev topic topic@{push}
	)
'

test_done
