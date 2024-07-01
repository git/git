#!/bin/sh

test_description='Test reffiles backend consistency check'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
GIT_TEST_DEFAULT_REF_FORMAT=files
export GIT_TEST_DEFAULT_REF_FORMAT

. ./test-lib.sh

test_expect_success 'ref name should be checked' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	(
		cd repo &&
		git commit --allow-empty -m initial &&
		git checkout -b branch-1 &&
		git tag tag-1 &&
		git commit --allow-empty -m second &&
		git checkout -b branch-2 &&
		git tag tag-2 &&
		git tag multi_hierarchy/tag-2
	) &&
	(
		cd repo &&
		cp $branch_dir_prefix/branch-1 $branch_dir_prefix/.branch-1 &&
		test_must_fail git fsck 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/.branch-1: badRefName: invalid refname format
		EOF
		rm $branch_dir_prefix/.branch-1 &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		cp $tag_dir_prefix/tag-1 $tag_dir_prefix/tag-1.lock &&
		test_must_fail git fsck 2>err &&
		cat >expect <<-EOF &&
		error: refs/tags/tag-1.lock: badRefName: invalid refname format
		EOF
		rm $tag_dir_prefix/tag-1.lock &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		cp $branch_dir_prefix/branch-1 $branch_dir_prefix/@ &&
		test_must_fail git fsck 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/@: badRefName: invalid refname format
		EOF
		rm $branch_dir_prefix/@ &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		cp $tag_dir_prefix/multi_hierarchy/tag-2 $tag_dir_prefix/multi_hierarchy/@ &&
		test_must_fail git fsck 2>err &&
		cat >expect <<-EOF &&
		error: refs/tags/multi_hierarchy/@: badRefName: invalid refname format
		EOF
		rm $tag_dir_prefix/multi_hierarchy/@ &&
		test_cmp expect err
	)
'

test_expect_success 'ref name check should be adapted into fsck messages' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	(
		cd repo &&
		git commit --allow-empty -m initial &&
		git checkout -b branch-1 &&
		git tag tag-1 &&
		git commit --allow-empty -m second &&
		git checkout -b branch-2 &&
		git tag tag-2
	) &&
	(
		cd repo &&
		cp $branch_dir_prefix/branch-1 $branch_dir_prefix/.branch-1 &&
		git -c fsck.badRefName=warn fsck 2>err &&
		cat >expect <<-EOF &&
		warning: refs/heads/.branch-1: badRefName: invalid refname format
		EOF
		rm $branch_dir_prefix/.branch-1 &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		cp $branch_dir_prefix/branch-1 $branch_dir_prefix/@ &&
		git -c fsck.badRefName=ignore fsck 2>err &&
		test_must_be_empty err
	)
'

test_expect_success 'regular ref content should be checked' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	(
		cd repo &&
		git commit --allow-empty -m initial &&
		git checkout -b branch-1 &&
		git tag tag-1 &&
		git commit --allow-empty -m second &&
		git checkout -b branch-2 &&
		git tag tag-2 &&
		git checkout -b a/b/tag-2
	) &&
	(
		cd repo &&
		printf "%s garbage" "$(git rev-parse branch-1)" > $branch_dir_prefix/branch-1-garbage &&
		git fsck 2>err &&
		cat >expect <<-EOF &&
		warning: refs/heads/branch-1-garbage: trailingRefContent: trailing garbage in ref
		EOF
		rm $branch_dir_prefix/branch-1-garbage &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		printf "%s garbage" "$(git rev-parse tag-1)" > $tag_dir_prefix/tag-1-garbage &&
		test_must_fail git -c fsck.trailingRefContent=error fsck 2>err &&
		cat >expect <<-EOF &&
		error: refs/tags/tag-1-garbage: trailingRefContent: trailing garbage in ref
		EOF
		rm $tag_dir_prefix/tag-1-garbage &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		printf "%s    " "$(git rev-parse tag-2)" > $tag_dir_prefix/tag-2-garbage &&
		git fsck 2>err &&
		cat >expect <<-EOF &&
		warning: refs/tags/tag-2-garbage: trailingRefContent: trailing garbage in ref
		EOF
		rm $tag_dir_prefix/tag-2-garbage &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		printf "xfsazqfxcadas" > $tag_dir_prefix/tag-2-bad &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/tags/tag-2-bad: badRefContent: invalid ref content
		EOF
		rm $tag_dir_prefix/tag-2-bad &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		printf "xfsazqfxcadas" > $branch_dir_prefix/a/b/branch-2-bad &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/a/b/branch-2-bad: badRefContent: invalid ref content
		EOF
		rm $branch_dir_prefix/a/b/branch-2-bad &&
		test_cmp expect err
	)
'

test_expect_success 'symbolic ref content should be checked' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	(
		cd repo &&
		git commit --allow-empty -m initial &&
		git checkout -b branch-1 &&
		git tag tag-1
	) &&
	(
		cd repo &&
		printf "ref: refs/heads/.branch" > $branch_dir_prefix/branch-2-bad &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/branch-2-bad: badSymrefPointee: point to invalid refname
		EOF
		rm $branch_dir_prefix/branch-2-bad &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		printf "ref: refs/heads" > $branch_dir_prefix/branch-2-bad &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/branch-2-bad: badSymrefPointee: point to invalid target
		EOF
		rm $branch_dir_prefix/branch-2-bad &&
		test_cmp expect err
	) &&
	(
		cd repo &&
		printf "ref: logs/maint-v2.45" > $branch_dir_prefix/branch-2-bad &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/branch-2-bad: badSymrefPointee: point to target out of refs hierarchy
		EOF
		rm $branch_dir_prefix/branch-2-bad &&
		test_cmp expect err
	)
'

test_done
