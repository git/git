#!/bin/sh
#
# Copyright (C) 2006 Carl D. Worth <cworth@cworth.org>
#

test_description='test git clone to cleanup after failure

This test covers the fact that if git clone fails, it should remove
the directory it created, to avoid the user having to manually
remove the directory before attempting a clone again.

Unless the directory already exists, in which case we clean up only what we
wrote.
'

. ./test-lib.sh

corrupt_repo () {
	test_when_finished "rmdir foo/.git/objects.bak" &&
	mkdir foo/.git/objects.bak/ &&
	test_when_finished "mv foo/.git/objects.bak/* foo/.git/objects/" &&
	mv foo/.git/objects/* foo/.git/objects.bak/
}

test_expect_success 'clone of non-existent source should fail' '
	test_must_fail git clone foo bar
'

test_expect_success 'failed clone should not leave a directory' '
	test_path_is_missing bar
'

test_expect_success 'create a repo to clone' '
	test_create_repo foo
'

test_expect_success 'create objects in repo for later corruption' '
	test_commit -C foo file
'

# source repository given to git clone should be relative to the
# current path not to the target dir
test_expect_success 'clone of non-existent (relative to $PWD) source should fail' '
	test_must_fail git clone ../foo baz
'

test_expect_success 'clone should work now that source exists' '
	git clone foo bar
'

test_expect_success 'successful clone must leave the directory' '
	test_path_is_dir bar
'

test_expect_success 'failed clone --separate-git-dir should not leave any directories' '
	corrupt_repo &&
	test_must_fail git clone --separate-git-dir gitdir foo worktree &&
	test_path_is_missing gitdir &&
	test_path_is_missing worktree
'

test_expect_success 'failed clone into empty leaves directory (vanilla)' '
	mkdir -p empty &&
	corrupt_repo &&
	test_must_fail git clone foo empty &&
	test_dir_is_empty empty
'

test_expect_success 'failed clone into empty leaves directory (bare)' '
	mkdir -p empty &&
	corrupt_repo &&
	test_must_fail git clone --bare foo empty &&
	test_dir_is_empty empty
'

test_expect_success 'failed clone into empty leaves directory (separate)' '
	mkdir -p empty-git empty-wt &&
	corrupt_repo &&
	test_must_fail git clone --separate-git-dir empty-git foo empty-wt &&
	test_dir_is_empty empty-git &&
	test_dir_is_empty empty-wt
'

test_expect_success 'failed clone into empty leaves directory (separate, git)' '
	mkdir -p empty-git &&
	corrupt_repo &&
	test_must_fail git clone --separate-git-dir empty-git foo no-wt &&
	test_dir_is_empty empty-git &&
	test_path_is_missing no-wt
'

test_expect_success 'failed clone into empty leaves directory (separate, wt)' '
	mkdir -p empty-wt &&
	corrupt_repo &&
	test_must_fail git clone --separate-git-dir no-git foo empty-wt &&
	test_path_is_missing no-git &&
	test_dir_is_empty empty-wt
'

test_done
