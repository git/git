#!/bin/sh
#
# Copyright (c) 2008 Nguyễn Thái Ngọc Duy
#

test_description='skip-worktree bit test'

. ./test-lib.sh

cat >expect.full <<EOF
H 1
H 2
H sub/1
H sub/2
EOF

cat >expect.skip <<EOF
S 1
H 2
S sub/1
H sub/2
EOF

test_expect_success 'setup' '
	mkdir sub &&
	touch ./1 ./2 sub/1 sub/2 &&
	git add 1 2 sub/1 sub/2 &&
	git ls-files -t | test_cmp expect.full -
'

test_expect_success 'index is at version 2' '
	test "$(test-index-version < .git/index)" = 2
'

test_expect_success 'update-index --skip-worktree' '
	git update-index --skip-worktree 1 sub/1 &&
	git ls-files -t | test_cmp expect.skip -
'

test_expect_success 'index is at version 3 after having some skip-worktree entries' '
	test "$(test-index-version < .git/index)" = 3
'

test_expect_success 'ls-files -t' '
	git ls-files -t | test_cmp expect.skip -
'

test_expect_success 'update-index --no-skip-worktree' '
	git update-index --no-skip-worktree 1 sub/1 &&
	git ls-files -t | test_cmp expect.full -
'

test_expect_success 'index version is back to 2 when there is no skip-worktree entry' '
	test "$(test-index-version < .git/index)" = 2
'

test_done
