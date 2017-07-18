#!/bin/sh

test_description='checkout must not overwrite an untracked objects'
. ./test-lib.sh

test_expect_success 'setup' '

	mkdir -p a/b/c &&
	>a/b/c/d &&
	git add -A &&
	git commit -m base &&
	git tag start
'

test_expect_success 'create a commit where dir a/b changed to file' '

	git checkout -b file &&
	rm -rf a/b &&
	>a/b &&
	git add -A &&
	git commit -m "dir to file"
'

test_expect_success 'checkout commit with dir must not remove untracked a/b' '

	git rm --cached a/b &&
	git commit -m "un-track the file" &&
	test_must_fail git checkout start &&
	test -f a/b
'

test_expect_success 'create a commit where dir a/b changed to symlink' '

	rm -rf a/b &&	# cleanup if previous test failed
	git checkout -f -b symlink start &&
	rm -rf a/b &&
	git add -A &&
	test_ln_s_add foo a/b &&
	git commit -m "dir to symlink"
'

test_expect_success 'checkout commit with dir must not remove untracked a/b' '

	git rm --cached a/b &&
	git commit -m "un-track the symlink" &&
	test_must_fail git checkout start
'

test_expect_success SYMLINKS 'the symlink remained' '

	test -h a/b
'

test_done
