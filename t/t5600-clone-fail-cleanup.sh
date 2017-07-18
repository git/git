#!/bin/sh
#
# Copyright (C) 2006 Carl D. Worth <cworth@cworth.org>
#

test_description='test git clone to cleanup after failure

This test covers the fact that if git clone fails, it should remove
the directory it created, to avoid the user having to manually
remove the directory before attempting a clone again.'

. ./test-lib.sh

test_expect_success \
    'clone of non-existent source should fail' \
    'test_must_fail git clone foo bar'

test_expect_success \
    'failed clone should not leave a directory' \
    '! test -d bar'

# Need a repo to clone
test_create_repo foo

# clone doesn't like it if there is no HEAD. Is that a bug?
(cd foo && touch file && git add file && git commit -m 'add file' >/dev/null 2>&1)

# source repository given to git clone should be relative to the
# current path not to the target dir
test_expect_success \
    'clone of non-existent (relative to $PWD) source should fail' \
    'test_must_fail git clone ../foo baz'

test_expect_success \
    'clone should work now that source exists' \
    'git clone foo bar'

test_expect_success \
    'successful clone must leave the directory' \
    'test -d bar'

test_expect_success 'failed clone --separate-git-dir should not leave any directories' '
	mkdir foo/.git/objects.bak/ &&
	mv foo/.git/objects/* foo/.git/objects.bak/ &&
	test_must_fail git clone --separate-git-dir gitdir foo worktree &&
	test_must_fail test -e gitdir &&
	test_must_fail test -e worktree &&
	mv foo/.git/objects.bak/* foo/.git/objects/ &&
	rmdir foo/.git/objects.bak
'

test_done
