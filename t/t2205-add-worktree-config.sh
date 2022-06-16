#!/bin/sh

test_description='directory traversal respects user config

This test verifies the traversal of the directory tree when the traversal begins
outside the repository.  Two instances for which this can occur are tested:

	1) The user manually sets the worktree.  For this instance, the test sets
	   the worktree two levels above the `.git` directory and checks whether we
	   are able to add to the index those files that are in either (1) the
	   manually configured worktree directory or (2) the standard worktree
	   location with respect to the `.git` directory (i.e. ensuring that the
	   encountered `.git` directory is not treated as belonging to a foreign
	   nested repository).
	2) The user manually sets the `git_dir` while the working directory is
	   outside the repository.  The test checks that files inside the
	   repository can be added to the index.
	'

. ./test-lib.sh

test_expect_success '1a: setup--config worktree' '
	mkdir test1 &&
	(
	cd test1 &&
	test_create_repo repo &&
	git --git-dir="repo/.git" config core.worktree "$(pwd)" &&

	mkdir -p outside-tracked outside-untracked &&
	mkdir -p repo/inside-tracked repo/inside-untracked &&
	>file-tracked &&
	>file-untracked &&
	>outside-tracked/file &&
	>outside-untracked/file &&
	>repo/file-tracked &&
	>repo/file-untracked &&
	>repo/inside-tracked/file &&
	>repo/inside-untracked/file &&

	cat >expect-tracked-unsorted <<-EOF &&
	../file-tracked
	../outside-tracked/file
	file-tracked
	inside-tracked/file
	EOF

	cat >expect-untracked-unsorted <<-EOF &&
	../file-untracked
	../outside-untracked/file
	file-untracked
	inside-untracked/file
	EOF

	cat >expect-all-dir-unsorted <<-EOF &&
	../file-untracked
	../file-tracked
	../outside-untracked/
	../outside-tracked/
	./
	EOF

	cat expect-tracked-unsorted expect-untracked-unsorted >expect-all-unsorted &&

	cat >.gitignore <<-EOF
	.gitignore
	actual-*
	expect-*
	EOF
	)
'

test_expect_success '1b: pre-add all' '
	(
	cd test1 &&
	local parent_dir="$(pwd)" &&
	git -C repo ls-files -o --exclude-standard "$parent_dir" >actual-all-unsorted &&
	sort actual-all-unsorted >actual-all &&
	sort expect-all-unsorted >expect-all &&
	test_cmp expect-all actual-all
	)
'

test_expect_success '1c: pre-add dir all' '
	(
	cd test1 &&
	local parent_dir="$(pwd)" &&
	git -C repo ls-files -o --directory --exclude-standard "$parent_dir" >actual-all-dir-unsorted &&
	sort actual-all-dir-unsorted >actual-all &&
	sort expect-all-dir-unsorted >expect-all &&
	test_cmp expect-all actual-all
	)
'

test_expect_success '1d: post-add tracked' '
	(
	cd test1 &&
	local parent_dir="$(pwd)" &&
	(
		cd repo &&
		git add file-tracked &&
		git add inside-tracked &&
		git add ../outside-tracked &&
		git add "$parent_dir/file-tracked" &&
		git ls-files "$parent_dir" >../actual-tracked-unsorted
	) &&
	sort actual-tracked-unsorted >actual-tracked &&
	sort expect-tracked-unsorted >expect-tracked &&
	test_cmp expect-tracked actual-tracked
	)
'

test_expect_success '1e: post-add untracked' '
	(
	cd test1 &&
	local parent_dir="$(pwd)" &&
	git -C repo ls-files -o --exclude-standard "$parent_dir" >actual-untracked-unsorted &&
	sort actual-untracked-unsorted >actual-untracked &&
	sort expect-untracked-unsorted >expect-untracked &&
	test_cmp expect-untracked actual-untracked
	)
'

test_expect_success '2a: setup--set git-dir' '
	mkdir test2 &&
	(
	cd test2 &&
	test_create_repo repo &&
	# create two foreign repositories that should remain untracked
	test_create_repo repo-outside &&
	test_create_repo repo/repo-inside &&

	mkdir -p repo/inside-tracked repo/inside-untracked &&
	>repo/file-tracked &&
	>repo/file-untracked &&
	>repo/inside-tracked/file &&
	>repo/inside-untracked/file &&
	>repo-outside/file &&
	>repo/repo-inside/file &&

	cat >expect-tracked-unsorted <<-EOF &&
	repo/file-tracked
	repo/inside-tracked/file
	EOF

	cat >expect-untracked-unsorted <<-EOF &&
	repo/file-untracked
	repo/inside-untracked/file
	repo/repo-inside/
	repo-outside/
	EOF

	cat >expect-all-dir-unsorted <<-EOF &&
	repo/
	repo-outside/
	EOF

	cat expect-tracked-unsorted expect-untracked-unsorted >expect-all-unsorted &&

	cat >.gitignore <<-EOF
	.gitignore
	actual-*
	expect-*
	EOF
	)
'

test_expect_success '2b: pre-add all' '
	(
	cd test2 &&
	git --git-dir=repo/.git ls-files -o --exclude-standard >actual-all-unsorted &&
	sort actual-all-unsorted >actual-all &&
	sort expect-all-unsorted >expect-all &&
	test_cmp expect-all actual-all
	)
'

test_expect_success '2c: pre-add dir all' '
	(
	cd test2 &&
	git --git-dir=repo/.git ls-files -o --directory --exclude-standard >actual-all-dir-unsorted &&
	sort actual-all-dir-unsorted >actual-all &&
	sort expect-all-dir-unsorted >expect-all &&
	test_cmp expect-all actual-all
	)
'

test_expect_success '2d: post-add tracked' '
	(
	cd test2 &&
	git --git-dir=repo/.git add repo/file-tracked &&
	git --git-dir=repo/.git add repo/inside-tracked &&
	git --git-dir=repo/.git ls-files >actual-tracked-unsorted &&
	sort actual-tracked-unsorted >actual-tracked &&
	sort expect-tracked-unsorted >expect-tracked &&
	test_cmp expect-tracked actual-tracked
	)
'

test_expect_success '2e: post-add untracked' '
	(
	cd test2 &&
	git --git-dir=repo/.git ls-files -o --exclude-standard >actual-untracked-unsorted &&
	sort actual-untracked-unsorted >actual-untracked &&
	sort expect-untracked-unsorted >expect-untracked &&
	test_cmp expect-untracked actual-untracked
	)
'

test_expect_success '3a: setup--add repo dir' '
	mkdir test3 &&
	(
	cd test3 &&
	test_create_repo repo &&

	mkdir -p repo/inside-tracked repo/inside-ignored &&
	>repo/file-tracked &&
	>repo/file-ignored &&
	>repo/inside-tracked/file &&
	>repo/inside-ignored/file &&

	cat >.gitignore <<-EOF &&
	.gitignore
	actual-*
	expect-*
	*ignored
	EOF

	cat >expect-tracked-unsorted <<-EOF &&
	repo/file-tracked
	repo/inside-tracked/file
	EOF

	cat >expect-ignored-unsorted <<-EOF
	repo/file-ignored
	repo/inside-ignored/
	.gitignore
	actual-ignored-unsorted
	expect-ignored-unsorted
	expect-tracked-unsorted
	EOF
	)
'

test_expect_success '3b: ignored' '
	(
	cd test3 &&
	git --git-dir=repo/.git ls-files -io --directory --exclude-standard >actual-ignored-unsorted &&
	sort actual-ignored-unsorted >actual-ignored &&
	sort expect-ignored-unsorted >expect-ignored &&
	test_cmp expect-ignored actual-ignored
	)
'

test_expect_success '3c: add repo' '
	(
	cd test3 &&
	git --git-dir=repo/.git add repo &&
	git --git-dir=repo/.git ls-files >actual-tracked-unsorted &&
	sort actual-tracked-unsorted >actual-tracked &&
	sort expect-tracked-unsorted >expect-tracked &&
	test_cmp expect-tracked actual-tracked
	)
'

test_done
