#!/bin/sh

test_description='checkout must not overwrite an untracked objects'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '

	mkdir -p a/b/c &&
	>a/b/c/d &&
	but add -A &&
	but cummit -m base &&
	but tag start
'

test_expect_success 'create a cummit where dir a/b changed to file' '

	but checkout -b file &&
	rm -rf a/b &&
	>a/b &&
	but add -A &&
	but cummit -m "dir to file"
'

test_expect_success 'checkout cummit with dir must not remove untracked a/b' '

	but rm --cached a/b &&
	but cummit -m "un-track the file" &&
	test_must_fail but checkout start &&
	test -f a/b
'

test_expect_success 'create a cummit where dir a/b changed to symlink' '

	rm -rf a/b &&	# cleanup if previous test failed
	but checkout -f -b symlink start &&
	rm -rf a/b &&
	but add -A &&
	test_ln_s_add foo a/b &&
	but cummit -m "dir to symlink"
'

test_expect_success 'checkout cummit with dir must not remove untracked a/b' '

	but rm --cached a/b &&
	but cummit -m "un-track the symlink" &&
	test_must_fail but checkout start
'

test_expect_success SYMLINKS 'the symlink remained' '

	test_when_finished "rm a/b" &&
	test -h a/b
'

test_expect_success SYMLINKS 'checkout -f must not follow symlinks when removing entries' '
	but checkout -f start &&
	mkdir dir &&
	>dir/f &&
	but add dir/f &&
	but cummit -m "add dir/f" &&
	mv dir untracked &&
	ln -s untracked dir &&
	but checkout -f HEAD~ &&
	test_path_is_file untracked/f
'

test_done
