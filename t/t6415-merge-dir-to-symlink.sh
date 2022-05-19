#!/bin/sh

test_description='merging when a directory was replaced with a symlink'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'create a cummit where dir a/b changed to symlink' '
	mkdir -p a/b/c a/b-2/c &&
	> a/b/c/d &&
	> a/b-2/c/d &&
	> a/x &&
	but add -A &&
	but cummit -m base &&
	but tag start &&
	rm -rf a/b &&
	but add -A &&
	test_ln_s_add b-2 a/b &&
	but cummit -m "dir to symlink"
'

test_expect_success 'checkout does not clobber untracked symlink' '
	but checkout HEAD^0 &&
	but reset --hard main &&
	but rm --cached a/b &&
	but cummit -m "untracked symlink remains" &&
	test_must_fail but checkout start^0 &&
	but clean -fd    # Do not leave the untracked symlink in the way
'

test_expect_success 'a/b-2/c/d is kept when clobbering symlink b' '
	but checkout HEAD^0 &&
	but reset --hard main &&
	but rm --cached a/b &&
	but cummit -m "untracked symlink remains" &&
	but checkout -f start^0 &&
	test_path_is_file a/b-2/c/d &&
	but clean -fd    # Do not leave the untracked symlink in the way
'

test_expect_success 'checkout should not have deleted a/b-2/c/d' '
	but checkout HEAD^0 &&
	but reset --hard main &&
	 but checkout start^0 &&
	 test_path_is_file a/b-2/c/d
'

test_expect_success 'setup for merge test' '
	but reset --hard &&
	test_path_is_file a/b-2/c/d &&
	echo x > a/x &&
	but add a/x &&
	but cummit -m x &&
	but tag baseline
'

test_expect_success 'Handle D/F conflict, do not lose a/b-2/c/d in merge (resolve)' '
	but reset --hard &&
	but checkout baseline^0 &&
	but merge -s resolve main &&
	test_path_is_file a/b-2/c/d
'

test_expect_success SYMLINKS 'a/b was resolved as symlink' '
	test -h a/b
'

test_expect_success 'Handle D/F conflict, do not lose a/b-2/c/d in merge (recursive)' '
	but reset --hard &&
	but checkout baseline^0 &&
	but merge -s recursive main &&
	test_path_is_file a/b-2/c/d
'

test_expect_success SYMLINKS 'a/b was resolved as symlink' '
	test -h a/b
'

test_expect_success 'Handle F/D conflict, do not lose a/b-2/c/d in merge (resolve)' '
	but reset --hard &&
	but checkout main^0 &&
	but merge -s resolve baseline^0 &&
	test_path_is_file a/b-2/c/d
'

test_expect_success SYMLINKS 'a/b was resolved as symlink' '
	test -h a/b
'

test_expect_success 'Handle F/D conflict, do not lose a/b-2/c/d in merge (recursive)' '
	but reset --hard &&
	but checkout main^0 &&
	but merge -s recursive baseline^0 &&
	test_path_is_file a/b-2/c/d
'

test_expect_success SYMLINKS 'a/b was resolved as symlink' '
	test -h a/b
'

test_expect_failure 'do not lose untracked in merge (resolve)' '
	but reset --hard &&
	but checkout baseline^0 &&
	>a/b/c/e &&
	test_must_fail but merge -s resolve main &&
	test_path_is_file a/b/c/e &&
	test_path_is_file a/b-2/c/d
'

test_expect_success 'do not lose untracked in merge (recursive)' '
	but reset --hard &&
	but checkout baseline^0 &&
	>a/b/c/e &&
	test_must_fail but merge -s recursive main &&
	test_path_is_file a/b/c/e &&
	test_path_is_file a/b-2/c/d
'

test_expect_success 'do not lose modifications in merge (resolve)' '
	but reset --hard &&
	but checkout baseline^0 &&
	echo more content >>a/b/c/d &&
	test_must_fail but merge -s resolve main
'

test_expect_success 'do not lose modifications in merge (recursive)' '
	but reset --hard &&
	but checkout baseline^0 &&
	echo more content >>a/b/c/d &&
	test_must_fail but merge -s recursive main
'

test_expect_success 'setup a merge where dir a/b-2 changed to symlink' '
	but reset --hard &&
	but checkout start^0 &&
	rm -rf a/b-2 &&
	but add -A &&
	test_ln_s_add b a/b-2 &&
	but cummit -m "dir a/b-2 to symlink" &&
	but tag test2
'

test_expect_success 'merge should not have D/F conflicts (resolve)' '
	but reset --hard &&
	but checkout baseline^0 &&
	but merge -s resolve test2 &&
	test_path_is_file a/b/c/d
'

test_expect_success SYMLINKS 'a/b-2 was resolved as symlink' '
	test -h a/b-2
'

test_expect_success 'merge should not have D/F conflicts (recursive)' '
	but reset --hard &&
	but checkout baseline^0 &&
	but merge -s recursive test2 &&
	test_path_is_file a/b/c/d
'

test_expect_success SYMLINKS 'a/b-2 was resolved as symlink' '
	test -h a/b-2
'

test_expect_success 'merge should not have F/D conflicts (recursive)' '
	but reset --hard &&
	but checkout -b foo test2 &&
	but merge -s recursive baseline^0 &&
	test_path_is_file a/b/c/d
'

test_expect_success SYMLINKS 'a/b-2 was resolved as symlink' '
	test -h a/b-2
'

test_done
