#!/bin/sh

test_description='setup: validation of .git file/directory types

Verify that setup_git_directory() correctly handles:
1. Valid .git directories (including symlinks to them).
2. Invalid .git files (FIFOs, sockets) by erroring out.
3. Invalid .git files (garbage) by erroring out.
'

. ./test-lib.sh

test_expect_success 'setup: create parent git repository' '
	git init parent &&
	test_commit -C parent "root-commit"
'

test_expect_success SYMLINKS 'setup: .git as a symlink to a directory is valid' '
	test_when_finished "rm -rf parent/link-to-dir" &&
	mkdir -p parent/link-to-dir &&
	(
		cd parent/link-to-dir &&
		git init real-repo &&
		ln -s real-repo/.git .git &&
		git rev-parse --git-dir >actual &&
		echo .git >expect &&
		test_cmp expect actual
	)
'

test_expect_success PIPE 'setup: .git as a FIFO (named pipe) is rejected' '
	test_when_finished "rm -rf parent/fifo-trap" &&
	mkdir -p parent/fifo-trap &&
	(
		cd parent/fifo-trap &&
		mkfifo .git &&
		test_must_fail git rev-parse --git-dir 2>stderr &&
		grep "not a regular file" stderr
	)
'

test_expect_success SYMLINKS,PIPE 'setup: .git as a symlink to a FIFO is rejected' '
	test_when_finished "rm -rf parent/symlink-fifo-trap" &&
	mkdir -p parent/symlink-fifo-trap &&
	(
		cd parent/symlink-fifo-trap &&
		mkfifo target-fifo &&
		ln -s target-fifo .git &&
		test_must_fail git rev-parse --git-dir 2>stderr &&
		grep "not a regular file" stderr
	)
'

test_expect_success 'setup: .git with garbage content is rejected' '
	test_when_finished "rm -rf parent/garbage-trap" &&
	mkdir -p parent/garbage-trap &&
	(
		cd parent/garbage-trap &&
		echo "garbage" >.git &&
		test_must_fail git rev-parse --git-dir 2>stderr &&
		grep "invalid gitfile format" stderr
	)
'

test_expect_success 'setup: .git as an empty directory is ignored' '
	test_when_finished "rm -rf parent/empty-dir" &&
	mkdir -p parent/empty-dir &&
	(
		cd parent/empty-dir &&
		git rev-parse --git-dir >expect &&
		mkdir .git &&
		git rev-parse --git-dir >actual &&
		test_cmp expect actual
	)
'

test_done
