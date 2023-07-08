#!/bin/sh

test_description='check the consisitency of behavior of --all and --branches'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

delete_refs() {
	dir=$1
	shift
	rm -rf deletes
	for arg in $*
	do
		echo "delete ${arg}" >>deletes
	done
	git -C $dir update-ref --stdin < deletes
}

test_expect_success 'setup bare remote' '
	git init --bare remote-1 &&
	git -C remote-1 config gc.auto 0 &&
	test_commit one &&
	git push remote-1 HEAD
'

test_expect_success 'setup different types of references' '
	cat >refs <<-EOF &&
	update refs/heads/branch-1 HEAD
	update refs/heads/branch-2 HEAD
	EOF

	git tag -a -m "annotated" annotated-1 HEAD &&
	git tag -a -m "annotated" annotated-2 HEAD &&
	git update-ref --stdin < refs
'

test_expect_success '--all and --branches have the same behavior' '
	test_when_finished "delete_refs remote-1 \
			   refs/heads/branch-1 \
			   refs/heads/branch-2" &&
	git push remote-1 --all &&
	commit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
	$commit refs/heads/branch-1
	$commit refs/heads/branch-2
	$commit refs/heads/main
	EOF

	git -C remote-1 show-ref --heads >actual.all &&
	delete_refs remote-1 refs/heads/branch-1 refs/heads/branch-2 &&
	git push remote-1 --branches &&
	git -C remote-1 show-ref --heads >actual.branches &&
	test_cmp actual.all actual.branches &&
	test_cmp expect actual.all
'

test_expect_success '--all or --branches can not be combined with refspecs' '
	test_must_fail git push remote-1 --all main >actual.all 2>&1 &&
	test_must_fail git push remote-1 --branches main >actual.branches 2>&1 &&
	test_cmp actual.all actual.branches &&
	grep "be combined with refspecs" actual.all
'

test_expect_success '--all or --branches can not be combined with --mirror' '
	test_must_fail git push remote-1 --all --mirror >actual.all 2>&1 &&
	test_must_fail git push remote-1 --branches --mirror >actual.branches 2>&1 &&
	test_cmp actual.all actual.branches &&
	grep "cannot be used together" actual.all
'

test_expect_success '--all or --branches can not be combined with --tags' '
	test_must_fail git push remote-1 --all --tags >actual.all 2>&1 &&
	test_must_fail git push remote-1 --branches --tags >actual.branches 2>&1 &&
	test_cmp actual.all actual.branches &&
	grep "cannot be used together" actual.all
'


test_expect_success '--all or --branches can not be combined with --delete' '
	test_must_fail git push remote-1 --all --delete >actual.all 2>&1 &&
	test_must_fail git push remote-1 --branches --delete >actual.branches 2>&1 &&
	test_cmp actual.all actual.branches &&
	grep "cannot be used together" actual.all
'

test_expect_success '--all or --branches combines with --follow-tags have same behavior' '
	test_when_finished "delete_refs remote-1 \
			   refs/heads/branch-1 \
			   refs/heads/branch-2 \
			   refs/tags/annotated-1 \
			   refs/tags/annotated-2" &&
	git push remote-1 --all --follow-tags &&
	git -C remote-1 show-ref > actual.all &&
	cat >expect <<-EOF &&
	$commit refs/heads/branch-1
	$commit refs/heads/branch-2
	$commit refs/heads/main
	$(git rev-parse annotated-1) refs/tags/annotated-1
	$(git rev-parse annotated-2) refs/tags/annotated-2
	EOF

	delete_refs remote-1 \
		    refs/heads/branch-1 \
		    refs/heads/branch-2 \
		    refs/tags/annotated-1 \
		    refs/tags/annotated-2 &&
	git push remote-1 --branches --follow-tags &&
	git -C remote-1 show-ref >actual.branches &&
	test_cmp actual.all actual.branches &&
	test_cmp expect actual.all
'

test_done
