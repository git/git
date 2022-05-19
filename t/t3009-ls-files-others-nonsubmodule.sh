#!/bin/sh

test_description='test but ls-files --others with non-submodule repositories

This test runs but ls-files --others with the following working tree:

    nonrepo-no-files/
      plain directory with no files
    nonrepo-untracked-file/
      plain directory with an untracked file
    repo-no-cummit-no-files/
      but repository without a cummit or a file
    repo-no-cummit-untracked-file/
      but repository without a cummit but with an untracked file
    repo-with-cummit-no-files/
      but repository with a cummit and no untracked files
    repo-with-cummit-untracked-file/
      but repository with a cummit and an untracked file
'

. ./test-lib.sh

test_expect_success 'setup: directories' '
	mkdir nonrepo-no-files/ &&
	mkdir nonrepo-untracked-file &&
	: >nonrepo-untracked-file/untracked &&
	but init repo-no-cummit-no-files &&
	but init repo-no-cummit-untracked-file &&
	: >repo-no-cummit-untracked-file/untracked &&
	but init repo-with-cummit-no-files &&
	but -C repo-with-cummit-no-files cummit --allow-empty -mmsg &&
	but init repo-with-cummit-untracked-file &&
	test_cummit -C repo-with-cummit-untracked-file msg &&
	: >repo-with-cummit-untracked-file/untracked
'

test_expect_success 'ls-files --others handles untracked but repositories' '
	but ls-files -o >output &&
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
