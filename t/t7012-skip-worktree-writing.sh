#!/bin/sh
#
# Copyright (c) 2008 Nguyễn Thái Ngọc Duy
#

test_description='test worktree writing operations when skip-worktree is used'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit init &&
	echo modified >> init.t &&
	touch added &&
	git add init.t added &&
	git commit -m "modified and added" &&
	git tag top
'

test_expect_success 'read-tree updates worktree, absent case' '
	git checkout -f top &&
	git update-index --skip-worktree init.t &&
	rm init.t &&
	git read-tree -m -u HEAD^ &&
	echo init > expected &&
	test_cmp expected init.t
'

test_expect_success 'read-tree updates worktree, dirty case' '
	git checkout -f top &&
	git update-index --skip-worktree init.t &&
	echo dirty >> init.t &&
	test_must_fail git read-tree -m -u HEAD^ &&
	grep -q dirty init.t &&
	test "$(git ls-files -t init.t)" = "S init.t" &&
	git update-index --no-skip-worktree init.t
'

test_expect_success 'read-tree removes worktree, absent case' '
	git checkout -f top &&
	git update-index --skip-worktree added &&
	rm added &&
	git read-tree -m -u HEAD^ &&
	test ! -f added
'

test_expect_success 'read-tree removes worktree, dirty case' '
	git checkout -f top &&
	git update-index --skip-worktree added &&
	echo dirty >> added &&
	test_must_fail git read-tree -m -u HEAD^ &&
	grep -q dirty added &&
	test "$(git ls-files -t added)" = "S added" &&
	git update-index --no-skip-worktree added
'

NULL_SHA1=e69de29bb2d1d6434b8b29ae775ad8c2e48c5391
ZERO_SHA0=0000000000000000000000000000000000000000
setup_absent() {
	test -f 1 && rm 1
	git update-index --remove 1 &&
	git update-index --add --cacheinfo 100644 $NULL_SHA1 1 &&
	git update-index --skip-worktree 1
}

test_absent() {
	echo "100644 $NULL_SHA1 0	1" > expected &&
	git ls-files --stage 1 > result &&
	test_cmp expected result &&
	test ! -f 1
}

setup_dirty() {
	git update-index --force-remove 1 &&
	echo dirty > 1 &&
	git update-index --add --cacheinfo 100644 $NULL_SHA1 1 &&
	git update-index --skip-worktree 1
}

test_dirty() {
	echo "100644 $NULL_SHA1 0	1" > expected &&
	git ls-files --stage 1 > result &&
	test_cmp expected result &&
	echo dirty > expected
	test_cmp expected 1
}

cat >expected <<EOF
S 1
H 2
H init.t
S sub/1
H sub/2
EOF

test_expect_success 'index setup' '
	git checkout -f init &&
	mkdir sub &&
	touch ./1 ./2 sub/1 sub/2 &&
	git add 1 2 sub/1 sub/2 &&
	git update-index --skip-worktree 1 sub/1 &&
	git ls-files -t > result &&
	test_cmp expected result
'

test_expect_success 'git-add ignores worktree content' '
	setup_absent &&
	git add 1 &&
	test_absent
'

test_expect_success 'git-add ignores worktree content' '
	setup_dirty &&
	git add 1 &&
	test_dirty
'

test_expect_success 'git-rm fails if worktree is dirty' '
	setup_dirty &&
	test_must_fail git rm 1 &&
	test_dirty
'

cat >expected <<EOF
Would remove expected
Would remove result
EOF
test_expect_success 'git-clean, absent case' '
	setup_absent &&
	git clean -n > result &&
	test_cmp expected result
'

test_expect_success 'git-clean, dirty case' '
	setup_dirty &&
	git clean -n > result &&
	test_cmp expected result
'

#TODO test_expect_failure 'git-apply adds file' false
#TODO test_expect_failure 'git-apply updates file' false
#TODO test_expect_failure 'git-apply removes file' false
#TODO test_expect_failure 'git-mv to skip-worktree' false
#TODO test_expect_failure 'git-mv from skip-worktree' false
#TODO test_expect_failure 'git-checkout' false

test_done
