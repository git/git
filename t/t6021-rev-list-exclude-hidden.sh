#!/bin/sh

test_description='git rev-list --exclude-hidden test'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit_bulk --id=commit --ref=refs/heads/branch 1 &&
	COMMIT=$(git rev-parse refs/heads/branch) &&
	test_commit_bulk --id=tag --ref=refs/tags/lightweight 1 &&
	TAG=$(git rev-parse refs/tags/lightweight) &&
	test_commit_bulk --id=hidden --ref=refs/hidden/commit 1 &&
	HIDDEN=$(git rev-parse refs/hidden/commit) &&
	test_commit_bulk --id=namespace --ref=refs/namespaces/namespace/refs/namespaced/commit 1 &&
	NAMESPACE=$(git rev-parse refs/namespaces/namespace/refs/namespaced/commit)
'

test_expect_success 'invalid section' '
	echo "fatal: unsupported section for hidden refs: unsupported" >expected &&
	test_must_fail git rev-list --exclude-hidden=unsupported 2>err &&
	test_cmp expected err
'

for section in fetch receive uploadpack
do
	test_expect_success "$section: passed multiple times" '
		echo "fatal: --exclude-hidden= passed more than once" >expected &&
		test_must_fail git rev-list --exclude-hidden=$section --exclude-hidden=$section 2>err &&
		test_cmp expected err
	'

	test_expect_success "$section: without hiddenRefs" '
		git rev-list --exclude-hidden=$section --all >out &&
		cat >expected <<-EOF &&
		$NAMESPACE
		$HIDDEN
		$TAG
		$COMMIT
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: hidden via transfer.hideRefs" '
		git -c transfer.hideRefs=refs/hidden/ rev-list --exclude-hidden=$section --all >out &&
		cat >expected <<-EOF &&
		$NAMESPACE
		$TAG
		$COMMIT
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: hidden via $section.hideRefs" '
		git -c $section.hideRefs=refs/hidden/ rev-list --exclude-hidden=$section --all >out &&
		cat >expected <<-EOF &&
		$NAMESPACE
		$TAG
		$COMMIT
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: respects both transfer.hideRefs and $section.hideRefs" '
		git -c transfer.hideRefs=refs/tags/ -c $section.hideRefs=refs/hidden/ rev-list --exclude-hidden=$section --all >out &&
		cat >expected <<-EOF &&
		$NAMESPACE
		$COMMIT
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: negation without hidden refs marks everything as uninteresting" '
		git rev-list --all --exclude-hidden=$section --not --all >out &&
		test_must_be_empty out
	'

	test_expect_success "$section: negation with hidden refs marks them as interesting" '
		git -c transfer.hideRefs=refs/hidden/ rev-list --all --exclude-hidden=$section --not --all >out &&
		cat >expected <<-EOF &&
		$HIDDEN
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: hidden refs and excludes work together" '
		git -c transfer.hideRefs=refs/hidden/ rev-list --exclude=refs/tags/* --exclude-hidden=$section --all >out &&
		cat >expected <<-EOF &&
		$NAMESPACE
		$COMMIT
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: excluded hidden refs get reset" '
		git -c transfer.hideRefs=refs/ rev-list --exclude-hidden=$section --all --all >out &&
		cat >expected <<-EOF &&
		$NAMESPACE
		$HIDDEN
		$TAG
		$COMMIT
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: excluded hidden refs can be used with multiple pseudo-refs" '
		git -c transfer.hideRefs=refs/ rev-list --exclude-hidden=$section --all --exclude-hidden=$section --all >out &&
		test_must_be_empty out
	'

	test_expect_success "$section: works with --glob" '
		git -c transfer.hideRefs=refs/hidden/ rev-list --exclude-hidden=$section --glob=refs/h* >out &&
		cat >expected <<-EOF &&
		$COMMIT
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: operates on stripped refs by default" '
		GIT_NAMESPACE=namespace git -c transfer.hideRefs=refs/namespaced/ rev-list --exclude-hidden=$section --all >out &&
		cat >expected <<-EOF &&
		$HIDDEN
		$TAG
		$COMMIT
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: does not hide namespace by default" '
		GIT_NAMESPACE=namespace git -c transfer.hideRefs=refs/namespaces/namespace/ rev-list --exclude-hidden=$section --all >out &&
		cat >expected <<-EOF &&
		$NAMESPACE
		$HIDDEN
		$TAG
		$COMMIT
		EOF
		test_cmp expected out
	'

	test_expect_success "$section: can operate on unstripped refs" '
		GIT_NAMESPACE=namespace git -c transfer.hideRefs=^refs/namespaces/namespace/ rev-list --exclude-hidden=$section --all >out &&
		cat >expected <<-EOF &&
		$HIDDEN
		$TAG
		$COMMIT
		EOF
		test_cmp expected out
	'

	for pseudoopt in remotes branches tags
	do
		test_expect_success "$section: fails with --$pseudoopt" '
			test_must_fail git rev-list --exclude-hidden=$section --$pseudoopt 2>err &&
			test_grep "error: --exclude-hidden cannot be used together with --$pseudoopt" err
		'

		test_expect_success "$section: fails with --$pseudoopt=pattern" '
			test_must_fail git rev-list --exclude-hidden=$section --$pseudoopt=pattern 2>err &&
			test_grep "error: --exclude-hidden cannot be used together with --$pseudoopt" err
		'
	done
done

test_done
