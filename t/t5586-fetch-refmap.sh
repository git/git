#!/bin/sh

test_description='"git fetch" with a remote.<name>.refmap but no remote.<name>.fetch

When a remote has a refmap configured but no fetch refspec, a
refspec-less fetch infers what to fetch from the local branches whose
@{upstream} is on that remote.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

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
		git config remote.upstream.refmap \
			"+refs/heads/*:refs/remotes/upstream/*"
	)
'

test_expect_success 'a bare fetch needs nothing until a branch is tracked' '
	(
		cd client &&
		git fetch upstream &&
		git for-each-ref --format="%(refname)" refs/remotes/upstream >actual &&
		test_must_be_empty actual
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
		git for-each-ref --format="%(refname)" refs/remotes/upstream >actual &&
		cat >expect <<-\EOF &&
		refs/remotes/upstream/HEAD
		refs/remotes/upstream/main
		refs/remotes/upstream/side
		EOF
		test_cmp expect actual &&
		git rev-parse refs/remotes/upstream/side >actual-oid &&
		git -C .. rev-parse side >expect-oid &&
		test_cmp expect-oid actual-oid
	)
'

test_expect_success 'a second branch tracking the same upstream branch does not fetch it twice' '
	(
		cd client &&
		git branch side-topic-2 upstream/side &&
		git fetch upstream &&
		git for-each-ref --format="%(refname)" refs/remotes/upstream >actual &&
		cat >expect <<-\EOF &&
		refs/remotes/upstream/HEAD
		refs/remotes/upstream/main
		refs/remotes/upstream/side
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'a branch tracking a different remote is not fetched from upstream' '
	(
		cd client &&
		git remote add other .. &&
		git fetch other next:refs/remotes/other/next &&
		git branch next-topic other/next &&
		git fetch upstream &&
		git for-each-ref --format="%(refname)" refs/remotes/upstream >actual &&
		cat >expect <<-\EOF &&
		refs/remotes/upstream/HEAD
		refs/remotes/upstream/main
		refs/remotes/upstream/side
		EOF
		test_cmp expect actual
	)
'

test_expect_success 'git remote show does not choke on a refmap-only remote' '
	(
		cd client &&
		git remote show upstream
	)
'

test_done
