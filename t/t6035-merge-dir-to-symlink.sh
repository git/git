#!/bin/sh

test_description='merging when a directory was replaced with a symlink'
. ./test-lib.sh

test_expect_success SYMLINKS 'create a commit where dir a/b changed to symlink' '
	mkdir -p a/b/c a/b-2/c &&
	> a/b/c/d &&
	> a/b-2/c/d &&
	> a/x &&
	git add -A &&
	git commit -m base &&
	git tag start &&
	rm -rf a/b &&
	ln -s b-2 a/b &&
	git add -A &&
	git commit -m "dir to symlink"
'

test_expect_success SYMLINKS 'checkout does not clobber untracked symlink' '
	git checkout HEAD^0 &&
	git reset --hard master &&
	git rm --cached a/b &&
	git commit -m "untracked symlink remains" &&
	test_must_fail git checkout start^0
'

test_expect_success SYMLINKS 'a/b-2/c/d is kept when clobbering symlink b' '
	git checkout HEAD^0 &&
	git reset --hard master &&
	git rm --cached a/b &&
	git commit -m "untracked symlink remains" &&
	git checkout -f start^0 &&
	test -f a/b-2/c/d
'

test_expect_success SYMLINKS 'checkout should not have deleted a/b-2/c/d' '
	git checkout HEAD^0 &&
	git reset --hard master &&
	 git checkout start^0 &&
	 test -f a/b-2/c/d
'

test_expect_success SYMLINKS 'setup for merge test' '
	git reset --hard &&
	test -f a/b-2/c/d &&
	echo x > a/x &&
	git add a/x &&
	git commit -m x &&
	git tag baseline
'

test_expect_success SYMLINKS 'Handle D/F conflict, do not lose a/b-2/c/d in merge (resolve)' '
	git reset --hard &&
	git checkout baseline^0 &&
	git merge -s resolve master &&
	test -h a/b &&
	test -f a/b-2/c/d
'

test_expect_success SYMLINKS 'Handle D/F conflict, do not lose a/b-2/c/d in merge (recursive)' '
	git reset --hard &&
	git checkout baseline^0 &&
	git merge -s recursive master &&
	test -h a/b &&
	test -f a/b-2/c/d
'

test_expect_success SYMLINKS 'Handle F/D conflict, do not lose a/b-2/c/d in merge (resolve)' '
	git reset --hard &&
	git checkout master^0 &&
	git merge -s resolve baseline^0 &&
	test -h a/b &&
	test -f a/b-2/c/d
'

test_expect_success SYMLINKS 'Handle F/D conflict, do not lose a/b-2/c/d in merge (recursive)' '
	git reset --hard &&
	git checkout master^0 &&
	git merge -s recursive baseline^0 &&
	test -h a/b &&
	test -f a/b-2/c/d
'

test_expect_failure SYMLINKS 'do not lose untracked in merge (resolve)' '
	git reset --hard &&
	git checkout baseline^0 &&
	>a/b/c/e &&
	test_must_fail git merge -s resolve master &&
	test -f a/b/c/e &&
	test -f a/b-2/c/d
'

test_expect_success SYMLINKS 'do not lose untracked in merge (recursive)' '
	git reset --hard &&
	git checkout baseline^0 &&
	>a/b/c/e &&
	test_must_fail git merge -s recursive master &&
	test -f a/b/c/e &&
	test -f a/b-2/c/d
'

test_expect_success SYMLINKS 'do not lose modifications in merge (resolve)' '
	git reset --hard &&
	git checkout baseline^0 &&
	echo more content >>a/b/c/d &&
	test_must_fail git merge -s resolve master
'

test_expect_success SYMLINKS 'do not lose modifications in merge (recursive)' '
	git reset --hard &&
	git checkout baseline^0 &&
	echo more content >>a/b/c/d &&
	test_must_fail git merge -s recursive master
'

test_expect_success SYMLINKS 'setup a merge where dir a/b-2 changed to symlink' '
	git reset --hard &&
	git checkout start^0 &&
	rm -rf a/b-2 &&
	ln -s b a/b-2 &&
	git add -A &&
	git commit -m "dir a/b-2 to symlink" &&
	git tag test2
'

test_expect_success SYMLINKS 'merge should not have D/F conflicts (resolve)' '
	git reset --hard &&
	git checkout baseline^0 &&
	git merge -s resolve test2 &&
	test -h a/b-2 &&
	test -f a/b/c/d
'

test_expect_success SYMLINKS 'merge should not have D/F conflicts (recursive)' '
	git reset --hard &&
	git checkout baseline^0 &&
	git merge -s recursive test2 &&
	test -h a/b-2 &&
	test -f a/b/c/d
'

test_expect_success SYMLINKS 'merge should not have F/D conflicts (recursive)' '
	git reset --hard &&
	git checkout -b foo test2 &&
	git merge -s recursive baseline^0 &&
	test -h a/b-2 &&
	test -f a/b/c/d
'

test_done
