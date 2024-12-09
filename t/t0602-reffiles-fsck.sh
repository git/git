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
	cd repo &&

	git commit --allow-empty -m initial &&
	git checkout -b default-branch &&
	git tag default-tag &&
	git tag multi_hierarchy/default-tag &&

	cp $branch_dir_prefix/default-branch $branch_dir_prefix/@ &&
	git refs verify 2>err &&
	test_must_be_empty err &&
	rm $branch_dir_prefix/@ &&

	cp $tag_dir_prefix/default-tag $tag_dir_prefix/tag-1.lock &&
	git refs verify 2>err &&
	rm $tag_dir_prefix/tag-1.lock &&
	test_must_be_empty err &&

	cp $tag_dir_prefix/default-tag $tag_dir_prefix/.lock &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/tags/.lock: badRefName: invalid refname format
	EOF
	rm $tag_dir_prefix/.lock &&
	test_cmp expect err &&

	for refname in ".refname-starts-with-dot" "~refname-has-stride"
	do
		cp $branch_dir_prefix/default-branch "$branch_dir_prefix/$refname" &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/$refname: badRefName: invalid refname format
		EOF
		rm "$branch_dir_prefix/$refname" &&
		test_cmp expect err || return 1
	done &&

	for refname in ".refname-starts-with-dot" "~refname-has-stride"
	do
		cp $tag_dir_prefix/default-tag "$tag_dir_prefix/$refname" &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/tags/$refname: badRefName: invalid refname format
		EOF
		rm "$tag_dir_prefix/$refname" &&
		test_cmp expect err || return 1
	done &&

	for refname in ".refname-starts-with-dot" "~refname-has-stride"
	do
		cp $tag_dir_prefix/multi_hierarchy/default-tag "$tag_dir_prefix/multi_hierarchy/$refname" &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/tags/multi_hierarchy/$refname: badRefName: invalid refname format
		EOF
		rm "$tag_dir_prefix/multi_hierarchy/$refname" &&
		test_cmp expect err || return 1
	done &&

	for refname in ".refname-starts-with-dot" "~refname-has-stride"
	do
		mkdir "$branch_dir_prefix/$refname" &&
		cp $branch_dir_prefix/default-branch "$branch_dir_prefix/$refname/default-branch" &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/$refname/default-branch: badRefName: invalid refname format
		EOF
		rm -r "$branch_dir_prefix/$refname" &&
		test_cmp expect err || return 1
	done
'

test_expect_success 'ref name check should be adapted into fsck messages' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	cd repo &&
	git commit --allow-empty -m initial &&
	git checkout -b branch-1 &&

	cp $branch_dir_prefix/branch-1 $branch_dir_prefix/.branch-1 &&
	git -c fsck.badRefName=warn refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/.branch-1: badRefName: invalid refname format
	EOF
	rm $branch_dir_prefix/.branch-1 &&
	test_cmp expect err &&

	cp $branch_dir_prefix/branch-1 $branch_dir_prefix/.branch-1 &&
	git -c fsck.badRefName=ignore refs verify 2>err &&
	test_must_be_empty err
'

test_expect_success 'ref name check should work for multiple worktrees' '
	test_when_finished "rm -rf repo" &&
	git init repo &&

	cd repo &&
	test_commit initial &&
	git checkout -b branch-1 &&
	test_commit second &&
	git checkout -b branch-2 &&
	test_commit third &&
	git checkout -b branch-3 &&
	git worktree add ./worktree-1 branch-1 &&
	git worktree add ./worktree-2 branch-2 &&
	worktree1_refdir_prefix=.git/worktrees/worktree-1/refs/worktree &&
	worktree2_refdir_prefix=.git/worktrees/worktree-2/refs/worktree &&

	(
		cd worktree-1 &&
		git update-ref refs/worktree/branch-4 refs/heads/branch-3
	) &&
	(
		cd worktree-2 &&
		git update-ref refs/worktree/branch-4 refs/heads/branch-3
	) &&

	cp $worktree1_refdir_prefix/branch-4 $worktree1_refdir_prefix/'\'' branch-5'\'' &&
	cp $worktree2_refdir_prefix/branch-4 $worktree2_refdir_prefix/'\''~branch-6'\'' &&

	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: worktrees/worktree-1/refs/worktree/ branch-5: badRefName: invalid refname format
	error: worktrees/worktree-2/refs/worktree/~branch-6: badRefName: invalid refname format
	EOF
	sort err >sorted_err &&
	test_cmp expect sorted_err &&

	for worktree in "worktree-1" "worktree-2"
	do
		(
			cd $worktree &&
			test_must_fail git refs verify 2>err &&
			cat >expect <<-EOF &&
			error: worktrees/worktree-1/refs/worktree/ branch-5: badRefName: invalid refname format
			error: worktrees/worktree-2/refs/worktree/~branch-6: badRefName: invalid refname format
			EOF
			sort err >sorted_err &&
			test_cmp expect sorted_err || return 1
		)
	done
'

test_expect_success 'regular ref content should be checked (individual)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	git refs verify 2>err &&
	test_must_be_empty err &&

	for bad_content in "$(git rev-parse main)x" "xfsazqfxcadas" "Xfsazqfxcadas"
	do
		printf "%s" $bad_content >$branch_dir_prefix/branch-bad &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/branch-bad: badRefContent: $bad_content
		EOF
		rm $branch_dir_prefix/branch-bad &&
		test_cmp expect err || return 1
	done &&

	for bad_content in "$(git rev-parse main)x" "xfsazqfxcadas" "Xfsazqfxcadas"
	do
		printf "%s" $bad_content >$branch_dir_prefix/a/b/branch-bad &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/a/b/branch-bad: badRefContent: $bad_content
		EOF
		rm $branch_dir_prefix/a/b/branch-bad &&
		test_cmp expect err || return 1
	done &&

	printf "%s" "$(git rev-parse main)" >$branch_dir_prefix/branch-no-newline &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-no-newline: refMissingNewline: misses LF at the end
	EOF
	rm $branch_dir_prefix/branch-no-newline &&
	test_cmp expect err &&

	for trailing_content in " garbage" "    more garbage"
	do
		printf "%s" "$(git rev-parse main)$trailing_content" >$branch_dir_prefix/branch-garbage &&
		git refs verify 2>err &&
		cat >expect <<-EOF &&
		warning: refs/heads/branch-garbage: trailingRefContent: has trailing garbage: '\''$trailing_content'\''
		EOF
		rm $branch_dir_prefix/branch-garbage &&
		test_cmp expect err || return 1
	done &&

	printf "%s\n\n\n" "$(git rev-parse main)" >$branch_dir_prefix/branch-garbage-special &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-garbage-special: trailingRefContent: has trailing garbage: '\''


	'\''
	EOF
	rm $branch_dir_prefix/branch-garbage-special &&
	test_cmp expect err &&

	printf "%s\n\n\n  garbage" "$(git rev-parse main)" >$branch_dir_prefix/branch-garbage-special &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-garbage-special: trailingRefContent: has trailing garbage: '\''


	  garbage'\''
	EOF
	rm $branch_dir_prefix/branch-garbage-special &&
	test_cmp expect err
'

test_expect_success 'regular ref content should be checked (aggregate)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	bad_content_1=$(git rev-parse main)x &&
	bad_content_2=xfsazqfxcadas &&
	bad_content_3=Xfsazqfxcadas &&
	printf "%s" $bad_content_1 >$tag_dir_prefix/tag-bad-1 &&
	printf "%s" $bad_content_2 >$tag_dir_prefix/tag-bad-2 &&
	printf "%s" $bad_content_3 >$branch_dir_prefix/a/b/branch-bad &&
	printf "%s" "$(git rev-parse main)" >$branch_dir_prefix/branch-no-newline &&
	printf "%s garbage" "$(git rev-parse main)" >$branch_dir_prefix/branch-garbage &&

	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/a/b/branch-bad: badRefContent: $bad_content_3
	error: refs/tags/tag-bad-1: badRefContent: $bad_content_1
	error: refs/tags/tag-bad-2: badRefContent: $bad_content_2
	warning: refs/heads/branch-garbage: trailingRefContent: has trailing garbage: '\'' garbage'\''
	warning: refs/heads/branch-no-newline: refMissingNewline: misses LF at the end
	EOF
	sort err >sorted_err &&
	test_cmp expect sorted_err
'

test_expect_success 'textual symref content should be checked (individual)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	for good_referent in "refs/heads/branch" "HEAD"
	do
		printf "ref: %s\n" $good_referent >$branch_dir_prefix/branch-good &&
		git refs verify 2>err &&
		rm $branch_dir_prefix/branch-good &&
		test_must_be_empty err || return 1
	done &&

	for bad_referent in "refs/heads/.branch" "refs/heads/~branch" "refs/heads/?branch"
	do
		printf "ref: %s\n" $bad_referent >$branch_dir_prefix/branch-bad &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: refs/heads/branch-bad: badReferentName: points to invalid refname '\''$bad_referent'\''
		EOF
		rm $branch_dir_prefix/branch-bad &&
		test_cmp expect err || return 1
	done &&

	printf "ref: refs/heads/branch" >$branch_dir_prefix/branch-no-newline &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-no-newline: refMissingNewline: misses LF at the end
	EOF
	rm $branch_dir_prefix/branch-no-newline &&
	test_cmp expect err &&

	printf "ref: refs/heads/branch     " >$branch_dir_prefix/a/b/branch-trailing-1 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/a/b/branch-trailing-1: refMissingNewline: misses LF at the end
	warning: refs/heads/a/b/branch-trailing-1: trailingRefContent: has trailing whitespaces or newlines
	EOF
	rm $branch_dir_prefix/a/b/branch-trailing-1 &&
	test_cmp expect err &&

	printf "ref: refs/heads/branch\n\n" >$branch_dir_prefix/a/b/branch-trailing-2 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/a/b/branch-trailing-2: trailingRefContent: has trailing whitespaces or newlines
	EOF
	rm $branch_dir_prefix/a/b/branch-trailing-2 &&
	test_cmp expect err &&

	printf "ref: refs/heads/branch \n" >$branch_dir_prefix/a/b/branch-trailing-3 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/a/b/branch-trailing-3: trailingRefContent: has trailing whitespaces or newlines
	EOF
	rm $branch_dir_prefix/a/b/branch-trailing-3 &&
	test_cmp expect err &&

	printf "ref: refs/heads/branch \n  " >$branch_dir_prefix/a/b/branch-complicated &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/a/b/branch-complicated: refMissingNewline: misses LF at the end
	warning: refs/heads/a/b/branch-complicated: trailingRefContent: has trailing whitespaces or newlines
	EOF
	rm $branch_dir_prefix/a/b/branch-complicated &&
	test_cmp expect err
'

test_expect_success 'textual symref content should be checked (aggregate)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	printf "ref: refs/heads/branch\n" >$branch_dir_prefix/branch-good &&
	printf "ref: HEAD\n" >$branch_dir_prefix/branch-head &&
	printf "ref: refs/heads/branch" >$branch_dir_prefix/branch-no-newline-1 &&
	printf "ref: refs/heads/branch     " >$branch_dir_prefix/a/b/branch-trailing-1 &&
	printf "ref: refs/heads/branch\n\n" >$branch_dir_prefix/a/b/branch-trailing-2 &&
	printf "ref: refs/heads/branch \n" >$branch_dir_prefix/a/b/branch-trailing-3 &&
	printf "ref: refs/heads/branch \n  " >$branch_dir_prefix/a/b/branch-complicated &&
	printf "ref: refs/heads/.branch\n" >$branch_dir_prefix/branch-bad-1 &&

	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/branch-bad-1: badReferentName: points to invalid refname '\''refs/heads/.branch'\''
	warning: refs/heads/a/b/branch-complicated: refMissingNewline: misses LF at the end
	warning: refs/heads/a/b/branch-complicated: trailingRefContent: has trailing whitespaces or newlines
	warning: refs/heads/a/b/branch-trailing-1: refMissingNewline: misses LF at the end
	warning: refs/heads/a/b/branch-trailing-1: trailingRefContent: has trailing whitespaces or newlines
	warning: refs/heads/a/b/branch-trailing-2: trailingRefContent: has trailing whitespaces or newlines
	warning: refs/heads/a/b/branch-trailing-3: trailingRefContent: has trailing whitespaces or newlines
	warning: refs/heads/branch-no-newline-1: refMissingNewline: misses LF at the end
	EOF
	sort err >sorted_err &&
	test_cmp expect sorted_err
'

test_expect_success 'the target of the textual symref should be checked' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	for good_referent in "refs/heads/branch" "HEAD" "refs/tags/tag"
	do
		printf "ref: %s\n" $good_referent >$branch_dir_prefix/branch-good &&
		git refs verify 2>err &&
		rm $branch_dir_prefix/branch-good &&
		test_must_be_empty err || return 1
	done &&

	for nonref_referent in "refs-back/heads/branch" "refs-back/tags/tag" "reflogs/refs/heads/branch"
	do
		printf "ref: %s\n" $nonref_referent >$branch_dir_prefix/branch-bad-1 &&
		git refs verify 2>err &&
		cat >expect <<-EOF &&
		warning: refs/heads/branch-bad-1: symrefTargetIsNotARef: points to non-ref target '\''$nonref_referent'\''
		EOF
		rm $branch_dir_prefix/branch-bad-1 &&
		test_cmp expect err || return 1
	done
'

test_expect_success SYMLINKS 'symlink symref content should be checked' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	ln -sf ./main $branch_dir_prefix/branch-symbolic-good &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-symbolic-good: symlinkRef: use deprecated symbolic link for symref
	EOF
	rm $branch_dir_prefix/branch-symbolic-good &&
	test_cmp expect err &&

	ln -sf ../../logs/branch-escape $branch_dir_prefix/branch-symbolic &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-symbolic: symlinkRef: use deprecated symbolic link for symref
	warning: refs/heads/branch-symbolic: symrefTargetIsNotARef: points to non-ref target '\''logs/branch-escape'\''
	EOF
	rm $branch_dir_prefix/branch-symbolic &&
	test_cmp expect err &&

	ln -sf ./"branch   " $branch_dir_prefix/branch-symbolic-bad &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-symbolic-bad: symlinkRef: use deprecated symbolic link for symref
	error: refs/heads/branch-symbolic-bad: badReferentName: points to invalid refname '\''refs/heads/branch   '\''
	EOF
	rm $branch_dir_prefix/branch-symbolic-bad &&
	test_cmp expect err &&

	ln -sf ./".tag" $tag_dir_prefix/tag-symbolic-1 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/tags/tag-symbolic-1: symlinkRef: use deprecated symbolic link for symref
	error: refs/tags/tag-symbolic-1: badReferentName: points to invalid refname '\''refs/tags/.tag'\''
	EOF
	rm $tag_dir_prefix/tag-symbolic-1 &&
	test_cmp expect err
'

test_expect_success SYMLINKS 'symlink symref content should be checked (worktree)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	cd repo &&
	test_commit default &&
	git branch branch-1 &&
	git branch branch-2 &&
	git branch branch-3 &&
	git worktree add ./worktree-1 branch-2 &&
	git worktree add ./worktree-2 branch-3 &&
	main_worktree_refdir_prefix=.git/refs/heads &&
	worktree1_refdir_prefix=.git/worktrees/worktree-1/refs/worktree &&
	worktree2_refdir_prefix=.git/worktrees/worktree-2/refs/worktree &&

	(
		cd worktree-1 &&
		git update-ref refs/worktree/branch-4 refs/heads/branch-1
	) &&
	(
		cd worktree-2 &&
		git update-ref refs/worktree/branch-4 refs/heads/branch-1
	) &&

	ln -sf ../../../../refs/heads/good-branch $worktree1_refdir_prefix/branch-symbolic-good &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: worktrees/worktree-1/refs/worktree/branch-symbolic-good: symlinkRef: use deprecated symbolic link for symref
	EOF
	rm $worktree1_refdir_prefix/branch-symbolic-good &&
	test_cmp expect err &&

	ln -sf ../../../../worktrees/worktree-1/good-branch $worktree2_refdir_prefix/branch-symbolic-good &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: worktrees/worktree-2/refs/worktree/branch-symbolic-good: symlinkRef: use deprecated symbolic link for symref
	EOF
	rm $worktree2_refdir_prefix/branch-symbolic-good &&
	test_cmp expect err &&

	ln -sf ../../worktrees/worktree-2/good-branch $main_worktree_refdir_prefix/branch-symbolic-good &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-symbolic-good: symlinkRef: use deprecated symbolic link for symref
	EOF
	rm $main_worktree_refdir_prefix/branch-symbolic-good &&
	test_cmp expect err &&

	ln -sf ../../../../logs/branch-escape $worktree1_refdir_prefix/branch-symbolic &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: worktrees/worktree-1/refs/worktree/branch-symbolic: symlinkRef: use deprecated symbolic link for symref
	warning: worktrees/worktree-1/refs/worktree/branch-symbolic: symrefTargetIsNotARef: points to non-ref target '\''logs/branch-escape'\''
	EOF
	rm $worktree1_refdir_prefix/branch-symbolic &&
	test_cmp expect err &&

	for bad_referent_name in ".tag" "branch   "
	do
		ln -sf ./"$bad_referent_name" $worktree1_refdir_prefix/bad-symbolic &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		warning: worktrees/worktree-1/refs/worktree/bad-symbolic: symlinkRef: use deprecated symbolic link for symref
		error: worktrees/worktree-1/refs/worktree/bad-symbolic: badReferentName: points to invalid refname '\''worktrees/worktree-1/refs/worktree/$bad_referent_name'\''
		EOF
		rm $worktree1_refdir_prefix/bad-symbolic &&
		test_cmp expect err &&

		ln -sf ../../../../refs/heads/"$bad_referent_name" $worktree1_refdir_prefix/bad-symbolic &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		warning: worktrees/worktree-1/refs/worktree/bad-symbolic: symlinkRef: use deprecated symbolic link for symref
		error: worktrees/worktree-1/refs/worktree/bad-symbolic: badReferentName: points to invalid refname '\''refs/heads/$bad_referent_name'\''
		EOF
		rm $worktree1_refdir_prefix/bad-symbolic &&
		test_cmp expect err &&

		ln -sf ./"$bad_referent_name" $worktree2_refdir_prefix/bad-symbolic &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		warning: worktrees/worktree-2/refs/worktree/bad-symbolic: symlinkRef: use deprecated symbolic link for symref
		error: worktrees/worktree-2/refs/worktree/bad-symbolic: badReferentName: points to invalid refname '\''worktrees/worktree-2/refs/worktree/$bad_referent_name'\''
		EOF
		rm $worktree2_refdir_prefix/bad-symbolic &&
		test_cmp expect err &&

		ln -sf ../../../../refs/heads/"$bad_referent_name" $worktree2_refdir_prefix/bad-symbolic &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		warning: worktrees/worktree-2/refs/worktree/bad-symbolic: symlinkRef: use deprecated symbolic link for symref
		error: worktrees/worktree-2/refs/worktree/bad-symbolic: badReferentName: points to invalid refname '\''refs/heads/$bad_referent_name'\''
		EOF
		rm $worktree2_refdir_prefix/bad-symbolic &&
		test_cmp expect err || return 1
	done
'

test_expect_success 'ref content checks should work with worktrees' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	cd repo &&
	test_commit default &&
	git branch branch-1 &&
	git branch branch-2 &&
	git branch branch-3 &&
	git worktree add ./worktree-1 branch-2 &&
	git worktree add ./worktree-2 branch-3 &&
	worktree1_refdir_prefix=.git/worktrees/worktree-1/refs/worktree &&
	worktree2_refdir_prefix=.git/worktrees/worktree-2/refs/worktree &&

	(
		cd worktree-1 &&
		git update-ref refs/worktree/branch-4 refs/heads/branch-1
	) &&
	(
		cd worktree-2 &&
		git update-ref refs/worktree/branch-4 refs/heads/branch-1
	) &&

	for bad_content in "$(git rev-parse HEAD)x" "xfsazqfxcadas" "Xfsazqfxcadas"
	do
		printf "%s" $bad_content >$worktree1_refdir_prefix/bad-branch-1 &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: worktrees/worktree-1/refs/worktree/bad-branch-1: badRefContent: $bad_content
		EOF
		rm $worktree1_refdir_prefix/bad-branch-1 &&
		test_cmp expect err || return 1
	done &&

	for bad_content in "$(git rev-parse HEAD)x" "xfsazqfxcadas" "Xfsazqfxcadas"
	do
		printf "%s" $bad_content >$worktree2_refdir_prefix/bad-branch-2 &&
		test_must_fail git refs verify 2>err &&
		cat >expect <<-EOF &&
		error: worktrees/worktree-2/refs/worktree/bad-branch-2: badRefContent: $bad_content
		EOF
		rm $worktree2_refdir_prefix/bad-branch-2 &&
		test_cmp expect err || return 1
	done &&

	printf "%s" "$(git rev-parse HEAD)" >$worktree1_refdir_prefix/branch-no-newline &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: worktrees/worktree-1/refs/worktree/branch-no-newline: refMissingNewline: misses LF at the end
	EOF
	rm $worktree1_refdir_prefix/branch-no-newline &&
	test_cmp expect err &&

	printf "%s garbage" "$(git rev-parse HEAD)" >$worktree1_refdir_prefix/branch-garbage &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: worktrees/worktree-1/refs/worktree/branch-garbage: trailingRefContent: has trailing garbage: '\'' garbage'\''
	EOF
	rm $worktree1_refdir_prefix/branch-garbage &&
	test_cmp expect err
'

test_done
