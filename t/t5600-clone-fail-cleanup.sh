#!/bin/sh
#
# Copyright (C) 2006 Carl D. Worth <cworth@cworth.org>
#

test_description='test but clone to cleanup after failure

This test covers the fact that if but clone fails, it should remove
the directory it created, to avoid the user having to manually
remove the directory before attempting a clone again.

Unless the directory already exists, in which case we clean up only what we
wrote.
'

. ./test-lib.sh

corrupt_repo () {
	test_when_finished "rmdir foo/.but/objects.bak" &&
	mkdir foo/.but/objects.bak/ &&
	test_when_finished "mv foo/.but/objects.bak/* foo/.but/objects/" &&
	mv foo/.but/objects/* foo/.but/objects.bak/
}

test_expect_success 'clone of non-existent source should fail' '
	test_must_fail but clone foo bar
'

test_expect_success 'failed clone should not leave a directory' '
	test_path_is_missing bar
'

test_expect_success 'create a repo to clone' '
	test_create_repo foo
'

test_expect_success 'create objects in repo for later corruption' '
	test_cummit -C foo file &&
	but -C foo checkout --detach &&
	test_cummit -C foo detached
'

# source repository given to but clone should be relative to the
# current path not to the target dir
test_expect_success 'clone of non-existent (relative to $PWD) source should fail' '
	test_must_fail but clone ../foo baz
'

test_expect_success 'clone should work now that source exists' '
	but clone foo bar
'

test_expect_success 'successful clone must leave the directory' '
	test_path_is_dir bar
'

test_expect_success 'failed clone --separate-but-dir should not leave any directories' '
	corrupt_repo &&
	test_must_fail but clone --separate-but-dir butdir foo worktree &&
	test_path_is_missing butdir &&
	test_path_is_missing worktree
'

test_expect_success 'failed clone into empty leaves directory (vanilla)' '
	mkdir -p empty &&
	corrupt_repo &&
	test_must_fail but clone foo empty &&
	test_dir_is_empty empty
'

test_expect_success 'failed clone into empty leaves directory (bare)' '
	mkdir -p empty &&
	corrupt_repo &&
	test_must_fail but clone --bare foo empty &&
	test_dir_is_empty empty
'

test_expect_success 'failed clone into empty leaves directory (separate)' '
	mkdir -p empty-but empty-wt &&
	corrupt_repo &&
	test_must_fail but clone --separate-but-dir empty-but foo empty-wt &&
	test_dir_is_empty empty-but &&
	test_dir_is_empty empty-wt
'

test_expect_success 'failed clone into empty leaves directory (separate, but)' '
	mkdir -p empty-but &&
	corrupt_repo &&
	test_must_fail but clone --separate-but-dir empty-but foo no-wt &&
	test_dir_is_empty empty-but &&
	test_path_is_missing no-wt
'

test_expect_success 'failed clone into empty leaves directory (separate, wt)' '
	mkdir -p empty-wt &&
	corrupt_repo &&
	test_must_fail but clone --separate-but-dir no-but foo empty-wt &&
	test_path_is_missing no-but &&
	test_dir_is_empty empty-wt
'

test_expect_success 'transport failure cleans up directory' '
	test_must_fail but clone --no-local \
		-u "f() { but-upload-pack \"\$@\"; return 1; }; f" \
		foo broken-clone &&
	test_path_is_missing broken-clone
'

test_done
