#!/bin/sh

test_description='test git ls-files --others with non-submodule repositories

This test runs git ls-files --others with the following working tree:

    nonrepo-no-files/
      plain directory with no files
    nonrepo-untracked-file/
      plain directory with an untracked file
    repo-no-commit-no-files/
      git repository without a commit or a file
    repo-no-commit-untracked-file/
      git repository without a commit but with an untracked file
    repo-with-commit-no-files/
      git repository with a commit and no untracked files
    repo-with-commit-untracked-file/
      git repository with a commit and an untracked file
'

. ./test-lib.sh

test_expect_success 'setup: directories' '
	mkdir nonrepo-no-files/ &&
	mkdir nonrepo-untracked-file &&
	: >nonrepo-untracked-file/untracked &&
	git init repo-no-commit-no-files &&
	git init repo-no-commit-untracked-file &&
	: >repo-no-commit-untracked-file/untracked &&
	git init repo-with-commit-no-files &&
	git -C repo-with-commit-no-files commit --allow-empty -mmsg &&
	git init repo-with-commit-untracked-file &&
	test_commit -C repo-with-commit-untracked-file msg &&
	: >repo-with-commit-untracked-file/untracked
'

test_expect_success 'ls-files --others handles untracked git repositories' '
	git ls-files -o >output &&
	cat >expect <<-EOF &&
	nonrepo-untracked-file/untracked
	output
	repo-no-commit-no-files/
	repo-no-commit-untracked-file/
	repo-with-commit-no-files/
	repo-with-commit-untracked-file/
	EOF
	test_cmp expect output
'

test_done
