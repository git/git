#!/bin/sh

test_description='merging when a directory was replaced with a symlink'
. ./test-lib.sh

if ! test_have_prereq SYMLINKS
then
	skip_all='Symbolic links not supported, skipping tests.'
	test_done
fi

test_expect_success 'create a commit where dir a/b changed to symlink' '
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

test_expect_success 'keep a/b-2/c/d across checkout' '
	git checkout HEAD^0 &&
	git reset --hard master &&
	git rm --cached a/b &&
	git commit -m "untracked symlink remains" &&
	 git checkout start^0 &&
	 test -f a/b-2/c/d
'

test_expect_success 'checkout should not have deleted a/b-2/c/d' '
	git checkout HEAD^0 &&
	git reset --hard master &&
	 git checkout start^0 &&
	 test -f a/b-2/c/d
'

test_expect_success 'setup for merge test' '
	git reset --hard &&
	test -f a/b-2/c/d &&
	echo x > a/x &&
	git add a/x &&
	git commit -m x &&
	git tag baseline
'

test_expect_success 'do not lose a/b-2/c/d in merge (resolve)' '
	git reset --hard &&
	git checkout baseline^0 &&
	git merge -s resolve master &&
	test -h a/b &&
	test -f a/b-2/c/d
'

test_expect_failure 'do not lose a/b-2/c/d in merge (recursive)' '
	git reset --hard &&
	git checkout baseline^0 &&
	git merge -s recursive master &&
	test -h a/b &&
	test -f a/b-2/c/d
'

test_expect_success 'setup a merge where dir a/b-2 changed to symlink' '
	git reset --hard &&
	git checkout start^0 &&
	rm -rf a/b-2 &&
	ln -s b a/b-2 &&
	git add -A &&
	git commit -m "dir a/b-2 to symlink" &&
	git tag test2
'

test_expect_success 'merge should not have conflicts (resolve)' '
	git reset --hard &&
	git checkout baseline^0 &&
	git merge -s resolve test2 &&
	test -h a/b-2 &&
	test -f a/b/c/d
'

test_expect_failure 'merge should not have conflicts (recursive)' '
	git reset --hard &&
	git checkout baseline^0 &&
	git merge -s recursive test2 &&
	test -h a/b-2 &&
	test -f a/b/c/d
'

test_done
