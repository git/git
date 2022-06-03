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

setup_absent() {
	test -f 1 && rm 1
	git update-index --remove 1 &&
	git update-index --add --cacheinfo 100644 $EMPTY_BLOB 1 &&
	git update-index --skip-worktree 1
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

test_expect_success '--ignore-skip-worktree-entries leaves worktree alone' '
	test_commit keep-me &&
	git update-index --skip-worktree keep-me.t &&
	rm keep-me.t &&

	: ignoring the worktree &&
	git update-index --remove --ignore-skip-worktree-entries keep-me.t &&
	git diff-index --cached --exit-code HEAD &&

	: not ignoring the worktree, a deletion is staged &&
	git update-index --remove keep-me.t &&
	test_must_fail git diff-index --cached --exit-code HEAD \
		--diff-filter=D -- keep-me.t
'

test_expect_success 'stash restore in sparse checkout' '
	test_create_repo stash-restore &&
	(
		cd stash-restore &&

		mkdir subdir &&
		echo A >subdir/A &&
		echo untouched >untouched &&
		echo removeme >removeme &&
		echo modified >modified &&
		git add . &&
		git commit -m Initial &&

		echo AA >>subdir/A &&
		echo addme >addme &&
		echo tweaked >>modified &&
		rm removeme &&
		git add addme &&

		git stash push &&

		git sparse-checkout set --no-cone subdir &&

		# Ensure after sparse-checkout we only have expected files
		cat >expect <<-EOF &&
		S modified
		S removeme
		H subdir/A
		S untouched
		EOF
		git ls-files -t >actual &&
		test_cmp expect actual &&

		test_path_is_missing addme &&
		test_path_is_missing modified &&
		test_path_is_missing removeme &&
		test_path_is_file    subdir/A &&
		test_path_is_missing untouched &&

		# Put a file in the working directory in the way
		echo in the way >modified &&
		test_must_fail git stash apply 2>error&&

		grep "changes.*would be overwritten by merge" error &&

		echo in the way >expect &&
		test_cmp expect modified &&
		git diff --quiet HEAD ":!modified" &&

		# ...and that working directory reflects the files correctly
		test_path_is_missing addme &&
		test_path_is_file    modified &&
		test_path_is_missing removeme &&
		test_path_is_file    subdir/A &&
		test_path_is_missing untouched
	)
'

#TODO test_expect_failure 'git-apply adds file' false
#TODO test_expect_failure 'git-apply updates file' false
#TODO test_expect_failure 'git-apply removes file' false
#TODO test_expect_failure 'git-mv to skip-worktree' false
#TODO test_expect_failure 'git-mv from skip-worktree' false
#TODO test_expect_failure 'git-checkout' false

test_done
