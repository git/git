#!/bin/sh

test_description='fetch/push involving ref namespaces'
. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	git init original &&
	(
		cd original &&
		echo 0 >count &&
		git add count &&
		test_commit 0 &&
		echo 1 >count &&
		git add count &&
		test_commit 1 &&
		git remote add pushee-namespaced "ext::git --namespace=namespace %s ../pushee" &&
		git remote add pushee-unnamespaced ../pushee
	) &&
	commit0=$(cd original && git rev-parse HEAD^) &&
	commit1=$(cd original && git rev-parse HEAD) &&
	git init pushee &&
	git init puller
'

test_expect_success 'pushing into a repository using a ref namespace' '
	(
		cd original &&
		git push pushee-namespaced master &&
		git ls-remote pushee-namespaced >actual &&
		printf "$commit1\trefs/heads/master\n" >expected &&
		test_cmp expected actual &&
		git push pushee-namespaced --tags &&
		git ls-remote pushee-namespaced >actual &&
		printf "$commit0\trefs/tags/0\n" >>expected &&
		printf "$commit1\trefs/tags/1\n" >>expected &&
		test_cmp expected actual &&
		# Verify that the GIT_NAMESPACE environment variable works as well
		GIT_NAMESPACE=namespace git ls-remote "ext::git %s ../pushee" >actual &&
		test_cmp expected actual &&
		# Verify that --namespace overrides GIT_NAMESPACE
		GIT_NAMESPACE=garbage git ls-remote pushee-namespaced >actual &&
		test_cmp expected actual &&
		# Try a namespace with no content
		git ls-remote "ext::git --namespace=garbage %s ../pushee" >actual &&
		test_cmp /dev/null actual &&
		git ls-remote pushee-unnamespaced >actual &&
		sed -e "s|refs/|refs/namespaces/namespace/refs/|" expected >expected.unnamespaced &&
		test_cmp expected.unnamespaced actual
	)
'

test_expect_success 'pulling from a repository using a ref namespace' '
	(
		cd puller &&
		git remote add -f pushee-namespaced "ext::git --namespace=namespace %s ../pushee" &&
		git for-each-ref refs/ >actual &&
		printf "$commit1 commit\trefs/remotes/pushee-namespaced/master\n" >expected &&
		printf "$commit0 commit\trefs/tags/0\n" >>expected &&
		printf "$commit1 commit\trefs/tags/1\n" >>expected &&
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
	git clone --mirror pushee mirror &&
	(
		cd mirror &&
		git for-each-ref refs/ >actual &&
		printf "$commit1 commit\trefs/namespaces/namespace/refs/heads/master\n" >expected &&
		printf "$commit0 commit\trefs/namespaces/namespace/refs/tags/0\n" >>expected &&
		printf "$commit1 commit\trefs/namespaces/namespace/refs/tags/1\n" >>expected &&
		test_cmp expected actual
	)
'

test_expect_success 'hide namespaced refs with transfer.hideRefs' '
	GIT_NAMESPACE=namespace \
		git -C pushee -c transfer.hideRefs=refs/tags \
		ls-remote "ext::git %s ." >actual &&
	printf "$commit1\trefs/heads/master\n" >expected &&
	test_cmp expected actual
'

test_expect_success 'check that transfer.hideRefs does not match unstripped refs' '
	GIT_NAMESPACE=namespace \
		git -C pushee -c transfer.hideRefs=refs/namespaces/namespace/refs/tags \
		ls-remote "ext::git %s ." >actual &&
	printf "$commit1\trefs/heads/master\n" >expected &&
	printf "$commit0\trefs/tags/0\n" >>expected &&
	printf "$commit1\trefs/tags/1\n" >>expected &&
	test_cmp expected actual
'

test_expect_success 'hide full refs with transfer.hideRefs' '
	GIT_NAMESPACE=namespace \
		git -C pushee -c transfer.hideRefs="^refs/namespaces/namespace/refs/tags" \
		ls-remote "ext::git %s ." >actual &&
	printf "$commit1\trefs/heads/master\n" >expected &&
	test_cmp expected actual
'

test_expect_success 'try to update a hidden ref' '
	test_config -C pushee transfer.hideRefs refs/heads/master &&
	test_must_fail git -C original push pushee-namespaced master
'

test_expect_success 'try to update a ref that is not hidden' '
	test_config -C pushee transfer.hideRefs refs/namespaces/namespace/refs/heads/master &&
	git -C original push pushee-namespaced master
'

test_expect_success 'try to update a hidden full ref' '
	test_config -C pushee transfer.hideRefs "^refs/namespaces/namespace/refs/heads/master" &&
	test_must_fail git -C original push pushee-namespaced master
'

test_done
