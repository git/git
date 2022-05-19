#!/bin/sh
#
# Copyright (c) 2008 Nguyễn Thái Ngọc Duy
#

test_description='test worktree writing operations when skip-worktree is used'

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit init &&
	echo modified >> init.t &&
	touch added &&
	but add init.t added &&
	but cummit -m "modified and added" &&
	but tag top
'

test_expect_success 'read-tree updates worktree, absent case' '
	but checkout -f top &&
	but update-index --skip-worktree init.t &&
	rm init.t &&
	but read-tree -m -u HEAD^ &&
	echo init > expected &&
	test_cmp expected init.t
'

test_expect_success 'read-tree updates worktree, dirty case' '
	but checkout -f top &&
	but update-index --skip-worktree init.t &&
	echo dirty >> init.t &&
	test_must_fail but read-tree -m -u HEAD^ &&
	grep -q dirty init.t &&
	test "$(but ls-files -t init.t)" = "S init.t" &&
	but update-index --no-skip-worktree init.t
'

test_expect_success 'read-tree removes worktree, absent case' '
	but checkout -f top &&
	but update-index --skip-worktree added &&
	rm added &&
	but read-tree -m -u HEAD^ &&
	test ! -f added
'

test_expect_success 'read-tree removes worktree, dirty case' '
	but checkout -f top &&
	but update-index --skip-worktree added &&
	echo dirty >> added &&
	test_must_fail but read-tree -m -u HEAD^ &&
	grep -q dirty added &&
	test "$(but ls-files -t added)" = "S added" &&
	but update-index --no-skip-worktree added
'

setup_absent() {
	test -f 1 && rm 1
	but update-index --remove 1 &&
	but update-index --add --cacheinfo 100644 $EMPTY_BLOB 1 &&
	but update-index --skip-worktree 1
}

setup_dirty() {
	but update-index --force-remove 1 &&
	echo dirty > 1 &&
	but update-index --add --cacheinfo 100644 $EMPTY_BLOB 1 &&
	but update-index --skip-worktree 1
}

test_dirty() {
	echo "100644 $EMPTY_BLOB 0	1" > expected &&
	but ls-files --stage 1 > result &&
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
	but checkout -f init &&
	mkdir sub &&
	touch ./1 ./2 sub/1 sub/2 &&
	but add 1 2 sub/1 sub/2 &&
	but update-index --skip-worktree 1 sub/1 &&
	but ls-files -t > result &&
	test_cmp expected result
'

test_expect_success 'but-rm fails if worktree is dirty' '
	setup_dirty &&
	test_must_fail but rm 1 &&
	test_dirty
'

cat >expected <<EOF
Would remove expected
Would remove result
EOF
test_expect_success 'but-clean, absent case' '
	setup_absent &&
	but clean -n > result &&
	test_cmp expected result
'

test_expect_success 'but-clean, dirty case' '
	setup_dirty &&
	but clean -n > result &&
	test_cmp expected result
'

test_expect_success '--ignore-skip-worktree-entries leaves worktree alone' '
	test_cummit keep-me &&
	but update-index --skip-worktree keep-me.t &&
	rm keep-me.t &&

	: ignoring the worktree &&
	but update-index --remove --ignore-skip-worktree-entries keep-me.t &&
	but diff-index --cached --exit-code HEAD &&

	: not ignoring the worktree, a deletion is staged &&
	but update-index --remove keep-me.t &&
	test_must_fail but diff-index --cached --exit-code HEAD \
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
		but add . &&
		but cummit -m Initial &&

		echo AA >>subdir/A &&
		echo addme >addme &&
		echo tweaked >>modified &&
		rm removeme &&
		but add addme &&

		but stash push &&

		but sparse-checkout set subdir &&

		# Ensure after sparse-checkout we only have expected files
		cat >expect <<-EOF &&
		S modified
		S removeme
		H subdir/A
		S untouched
		EOF
		but ls-files -t >actual &&
		test_cmp expect actual &&

		test_path_is_missing addme &&
		test_path_is_missing modified &&
		test_path_is_missing removeme &&
		test_path_is_file    subdir/A &&
		test_path_is_missing untouched &&

		# Put a file in the working directory in the way
		echo in the way >modified &&
		test_must_fail but stash apply 2>error&&

		grep "changes.*would be overwritten by merge" error &&

		echo in the way >expect &&
		test_cmp expect modified &&
		but diff --quiet HEAD ":!modified" &&

		# ...and that working directory reflects the files correctly
		test_path_is_missing addme &&
		test_path_is_file    modified &&
		test_path_is_missing removeme &&
		test_path_is_file    subdir/A &&
		test_path_is_missing untouched
	)
'

#TODO test_expect_failure 'but-apply adds file' false
#TODO test_expect_failure 'but-apply updates file' false
#TODO test_expect_failure 'but-apply removes file' false
#TODO test_expect_failure 'but-mv to skip-worktree' false
#TODO test_expect_failure 'but-mv from skip-worktree' false
#TODO test_expect_failure 'but-checkout' false

test_done
