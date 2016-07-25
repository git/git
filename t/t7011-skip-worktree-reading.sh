#!/bin/sh
#
# Copyright (c) 2008 Nguyễn Thái Ngọc Duy
#

test_description='skip-worktree bit test'

. ./test-lib.sh

cat >expect.full <<EOF
H 1
H 2
H init.t
H sub/1
H sub/2
EOF

cat >expect.skip <<EOF
S 1
H 2
H init.t
S sub/1
H sub/2
EOF

setup_absent() {
	test -f 1 && rm 1
	git update-index --remove 1 &&
	git update-index --add --cacheinfo 100644 $EMPTY_BLOB 1 &&
	git update-index --skip-worktree 1
}

test_absent() {
	echo "100644 $EMPTY_BLOB 0	1" > expected &&
	git ls-files --stage 1 > result &&
	test_cmp expected result &&
	test ! -f 1
}

setup_dirty() {
	git update-index --force-remove 1 &&
	echo dirty > 1 &&
	git update-index --add --cacheinfo 100644 $EMPTY_BLOB 1 &&
	git update-index --skip-worktree 1
}

test_dirty() {
	echo "100644 $EMPTY_BLOB 0	1" > expected &&
	git ls-files --stage 1 > result &&
	test_cmp expected result &&
	echo dirty > expected
	test_cmp expected 1
}

test_expect_success 'setup' '
	test_commit init &&
	mkdir sub &&
	touch ./1 ./2 sub/1 sub/2 &&
	git add 1 2 sub/1 sub/2 &&
	git update-index --skip-worktree 1 sub/1 &&
	git ls-files -t > result &&
	test_cmp expect.skip result
'

test_expect_success 'update-index' '
	setup_absent &&
	git update-index 1 &&
	test_absent
'

test_expect_success 'update-index' '
	setup_dirty &&
	git update-index 1 &&
	test_dirty
'

test_expect_success 'update-index --remove' '
	setup_absent &&
	git update-index --remove 1 &&
	test -z "$(git ls-files 1)" &&
	test ! -f 1
'

test_expect_success 'update-index --remove' '
	setup_dirty &&
	git update-index --remove 1 &&
	test -z "$(git ls-files 1)" &&
	echo dirty > expected &&
	test_cmp expected 1
'

test_expect_success 'ls-files --deleted' '
	setup_absent &&
	test -z "$(git ls-files -d)"
'

test_expect_success 'ls-files --deleted' '
	setup_dirty &&
	test -z "$(git ls-files -d)"
'

test_expect_success 'ls-files --modified' '
	setup_absent &&
	test -z "$(git ls-files -m)"
'

test_expect_success 'ls-files --modified' '
	setup_dirty &&
	test -z "$(git ls-files -m)"
'

test_expect_success 'grep with skip-worktree file' '
	git update-index --no-skip-worktree 1 &&
	echo test > 1 &&
	git update-index 1 &&
	git update-index --skip-worktree 1 &&
	rm 1 &&
	test "$(git grep --no-ext-grep test)" = "1:test"
'

echo ":000000 100644 $_z40 $EMPTY_BLOB A	1" > expected
test_expect_success 'diff-index does not examine skip-worktree absent entries' '
	setup_absent &&
	git diff-index HEAD -- 1 > result &&
	test_cmp expected result
'

test_expect_success 'diff-index does not examine skip-worktree dirty entries' '
	setup_dirty &&
	git diff-index HEAD -- 1 > result &&
	test_cmp expected result
'

test_expect_success 'diff-files does not examine skip-worktree absent entries' '
	setup_absent &&
	test -z "$(git diff-files -- one)"
'

test_expect_success 'diff-files does not examine skip-worktree dirty entries' '
	setup_dirty &&
	test -z "$(git diff-files -- one)"
'

test_expect_success 'git-rm succeeds on skip-worktree absent entries' '
	setup_absent &&
	git rm 1
'

test_expect_success 'commit on skip-worktree absent entries' '
	git reset &&
	setup_absent &&
	test_must_fail git commit -m null 1
'

test_expect_success 'commit on skip-worktree dirty entries' '
	git reset &&
	setup_dirty &&
	test_must_fail git commit -m null 1
'

test_done
