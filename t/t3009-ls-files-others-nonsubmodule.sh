#!/bin/sh

test_description='test git ls-files --others with non-submodule repositories

This test runs git ls-files --others with the following working tree:

    nonrepo-no-files/
      plain directory with no files
    nonrepo-untracked-file/
      plain directory with an untracked file
    repo-no-cummit-no-files/
      git repository without a cummit or a file
    repo-no-cummit-untracked-file/
      git repository without a cummit but with an untracked file
    repo-with-cummit-no-files/
      git repository with a cummit and no untracked files
    repo-with-cummit-untracked-file/
      git repository with a cummit and an untracked file
'

. ./test-lib.sh

test_expect_success 'setup: directories' '
	mkdir nonrepo-no-files/ &&
	mkdir nonrepo-untracked-file &&
	: >nonrepo-untracked-file/untracked &&
	git init repo-no-cummit-no-files &&
	git init repo-no-cummit-untracked-file &&
	: >repo-no-cummit-untracked-file/untracked &&
	git init repo-with-cummit-no-files &&
	git -C repo-with-cummit-no-files cummit --allow-empty -mmsg &&
	git init repo-with-cummit-untracked-file &&
	test_cummit -C repo-with-cummit-untracked-file msg &&
	: >repo-with-cummit-untracked-file/untracked
'

test_expect_success 'ls-files --others handles untracked git repositories' '
	git ls-files -o >output &&
	cat >expect <<-EOF &&
	nonrepo-untracked-file/untracked
	output
	repo-no-cummit-no-files/
	repo-no-cummit-untracked-file/
	repo-with-cummit-no-files/
	repo-with-cummit-untracked-file/
	EOF
	test_cmp expect output
'

test_done
