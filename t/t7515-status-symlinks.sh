#!/bin/sh

test_description='git status and symlinks'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	echo .gitignore >.gitignore &&
	echo actual >>.gitignore &&
	echo expect >>.gitignore &&
	mkdir dir &&
	echo x >dir/file1 &&
	echo y >dir/file2 &&
	git add dir &&
	git commit -m initial &&
	git tag initial
'

test_expect_success SYMLINKS 'symlink to a directory' '
	test_when_finished "rm symlink" &&
	ln -s dir symlink &&
	echo "?? symlink" >expect &&
	git status --porcelain >actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'symlink replacing a directory' '
	test_when_finished "rm -rf copy && git reset --hard initial" &&
	mkdir copy &&
	cp dir/file1 copy/file1 &&
	echo "changed in copy" >copy/file2 &&
	git add copy &&
	git commit -m second &&
	rm -rf copy &&
	ln -s dir copy &&
	echo " D copy/file1" >expect &&
	echo " D copy/file2" >>expect &&
	echo "?? copy" >>expect &&
	git status --porcelain >actual &&
	test_cmp expect actual
'

test_done
