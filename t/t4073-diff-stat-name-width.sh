#!/bin/sh

test_description='git-diff check diffstat filepaths length when containing UTF-8 chars'

. ./test-lib.sh


create_files () {
	mkdir -p "d你好" &&
	touch "d你好/f再见"
}

test_expect_success 'setup' '
	git init &&
	git config core.quotepath off &&
	git commit -m "Initial commit" --allow-empty &&
	create_files &&
	git add . &&
	git commit -m "Added files"
'

test_expect_success 'test name-width long enough for filepath' '
	git diff HEAD~1 HEAD --stat --stat-name-width=12 >out &&
	grep "d你好/f再见 |" out &&
	git diff HEAD~1 HEAD --stat --stat-name-width=11 >out &&
	grep "d你好/f再见 |" out
'

test_expect_success 'test name-width not long enough for dir name' '
	git diff HEAD~1 HEAD --stat --stat-name-width=10 >out &&
	grep ".../f再见  |" out &&
	git diff HEAD~1 HEAD --stat --stat-name-width=9 >out &&
	grep ".../f再见 |" out
'

test_expect_success 'test name-width not long enough for slash' '
	git diff HEAD~1 HEAD --stat --stat-name-width=8 >out &&
	grep "...f再见 |" out
'

test_expect_success 'test name-width not long enough for file name' '
	git diff HEAD~1 HEAD --stat --stat-name-width=7 >out &&
	grep "...再见 |" out &&
	git diff HEAD~1 HEAD --stat --stat-name-width=6 >out &&
	grep "...见  |" out &&
	git diff HEAD~1 HEAD --stat --stat-name-width=5 >out &&
	grep "...见 |" out &&
	git diff HEAD~1 HEAD --stat --stat-name-width=4 >out &&
	grep "...  |" out
'

test_expect_success 'test name-width minimum length' '
	git diff HEAD~1 HEAD --stat --stat-name-width=3 >out &&
	grep "... |" out &&
	git diff HEAD~1 HEAD --stat --stat-name-width=2 >out &&
	grep "... |" out &&
	git diff HEAD~1 HEAD --stat --stat-name-width=1 >out &&
	grep "... |" out
'

test_done
