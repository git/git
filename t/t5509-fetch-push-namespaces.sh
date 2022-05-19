#!/bin/sh

test_description='fetch/push involving ref namespaces'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	but config --global protocol.ext.allow user &&
	test_tick &&
	but init original &&
	(
		cd original &&
		echo 0 >count &&
		but add count &&
		test_cummit 0 &&
		echo 1 >count &&
		but add count &&
		test_cummit 1 &&
		but remote add pushee-namespaced "ext::but --namespace=namespace %s ../pushee" &&
		but remote add pushee-unnamespaced ../pushee
	) &&
	cummit0=$(cd original && but rev-parse HEAD^) &&
	cummit1=$(cd original && but rev-parse HEAD) &&
	but init --bare pushee &&
	but init puller
'

test_expect_success 'pushing into a repository using a ref namespace' '
	(
		cd original &&
		but push pushee-namespaced main &&
		but ls-remote pushee-namespaced >actual &&
		printf "$cummit1\trefs/heads/main\n" >expected &&
		test_cmp expected actual &&
		but push pushee-namespaced --tags &&
		but ls-remote pushee-namespaced >actual &&
		printf "$cummit0\trefs/tags/0\n" >>expected &&
		printf "$cummit1\trefs/tags/1\n" >>expected &&
		test_cmp expected actual &&
		# Verify that the GIT_NAMESPACE environment variable works as well
		GIT_NAMESPACE=namespace but ls-remote "ext::but %s ../pushee" >actual &&
		test_cmp expected actual &&
		# Verify that --namespace overrides GIT_NAMESPACE
		GIT_NAMESPACE=garbage but ls-remote pushee-namespaced >actual &&
		test_cmp expected actual &&
		# Try a namespace with no content
		but ls-remote "ext::but --namespace=garbage %s ../pushee" >actual &&
		test_must_be_empty actual &&
		but ls-remote pushee-unnamespaced >actual &&
		sed -e "s|refs/|refs/namespaces/namespace/refs/|" expected >expected.unnamespaced &&
		test_cmp expected.unnamespaced actual
	)
'

test_expect_success 'pulling from a repository using a ref namespace' '
	(
		cd puller &&
		but remote add -f pushee-namespaced "ext::but --namespace=namespace %s ../pushee" &&
		but for-each-ref refs/ >actual &&
		printf "$cummit1 cummit\trefs/remotes/pushee-namespaced/main\n" >expected &&
		printf "$cummit0 cummit\trefs/tags/0\n" >>expected &&
		printf "$cummit1 cummit\trefs/tags/1\n" >>expected &&
		test_cmp expected actual
	)
'

# This test with clone --mirror checks for possible regressions in clone
# or the machinery underneath it. It ensures that no future change
# causes clone to ignore refs in refs/namespaces/*. In particular, it
# protects against a regression caused by any future change to the refs
# machinery that might cause it to ignore refs outside of refs/heads/*
# or refs/tags/*. More generally, this test also checks the high-level
# functionality of using clone --mirror to back up a set of repos hosted
# in the namespaces of a single repo.
test_expect_success 'mirroring a repository using a ref namespace' '
	but clone --mirror pushee mirror &&
	(
		cd mirror &&
		but for-each-ref refs/ >actual &&
		printf "$cummit1 cummit\trefs/namespaces/namespace/refs/heads/main\n" >expected &&
		printf "$cummit0 cummit\trefs/namespaces/namespace/refs/tags/0\n" >>expected &&
		printf "$cummit1 cummit\trefs/namespaces/namespace/refs/tags/1\n" >>expected &&
		test_cmp expected actual
	)
'

test_expect_success 'hide namespaced refs with transfer.hideRefs' '
	GIT_NAMESPACE=namespace \
		but -C pushee -c transfer.hideRefs=refs/tags \
		ls-remote "ext::but %s ." >actual &&
	printf "$cummit1\trefs/heads/main\n" >expected &&
	test_cmp expected actual
'

test_expect_success 'check that transfer.hideRefs does not match unstripped refs' '
	GIT_NAMESPACE=namespace \
		but -C pushee -c transfer.hideRefs=refs/namespaces/namespace/refs/tags \
		ls-remote "ext::but %s ." >actual &&
	printf "$cummit1\trefs/heads/main\n" >expected &&
	printf "$cummit0\trefs/tags/0\n" >>expected &&
	printf "$cummit1\trefs/tags/1\n" >>expected &&
	test_cmp expected actual
'

test_expect_success 'hide full refs with transfer.hideRefs' '
	GIT_NAMESPACE=namespace \
		but -C pushee -c transfer.hideRefs="^refs/namespaces/namespace/refs/tags" \
		ls-remote "ext::but %s ." >actual &&
	printf "$cummit1\trefs/heads/main\n" >expected &&
	test_cmp expected actual
'

test_expect_success 'try to update a hidden ref' '
	test_config -C pushee transfer.hideRefs refs/heads/main &&
	test_must_fail but -C original push pushee-namespaced main
'

test_expect_success 'try to update a ref that is not hidden' '
	test_config -C pushee transfer.hideRefs refs/namespaces/namespace/refs/heads/main &&
	but -C original push pushee-namespaced main
'

test_expect_success 'try to update a hidden full ref' '
	test_config -C pushee transfer.hideRefs "^refs/namespaces/namespace/refs/heads/main" &&
	test_must_fail but -C original push pushee-namespaced main
'

test_expect_success 'set up ambiguous HEAD' '
	but init ambiguous &&
	(
		cd ambiguous &&
		but cummit --allow-empty -m foo &&
		but update-ref refs/namespaces/ns/refs/heads/one HEAD &&
		but update-ref refs/namespaces/ns/refs/heads/two HEAD &&
		but symbolic-ref refs/namespaces/ns/HEAD \
			refs/namespaces/ns/refs/heads/two
	)
'

test_expect_success 'clone chooses correct HEAD (v0)' '
	GIT_NAMESPACE=ns but -c protocol.version=0 \
		clone ambiguous ambiguous-v0 &&
	echo refs/heads/two >expect &&
	but -C ambiguous-v0 symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'clone chooses correct HEAD (v2)' '
	GIT_NAMESPACE=ns but -c protocol.version=2 \
		clone ambiguous ambiguous-v2 &&
	echo refs/heads/two >expect &&
	but -C ambiguous-v2 symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'denyCurrentBranch and unborn branch with ref namespace' '
	(
		cd original &&
		but init unborn &&
		but remote add unborn-namespaced "ext::but --namespace=namespace %s unborn" &&
		test_must_fail but push unborn-namespaced HEAD:main &&
		but -C unborn config receive.denyCurrentBranch updateInstead &&
		but push unborn-namespaced HEAD:main
	)
'

test_done
