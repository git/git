#!/bin/sh

test_description='behavior of diff with symmetric-diff setups and --merge-base'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# build these situations:
#  - normal merge with one merge base (br1...b2r);
#  - criss-cross merge ie 2 merge bases (br1...main);
#  - disjoint subgraph (orphan branch, br3...main).
#
#     B---E   <-- main
#    / \ /
#   A   X
#    \ / \
#     C---D--G   <-- br1
#      \    /
#       ---F   <-- br2
#
#  H  <-- br3
#
# We put files into a few commits so that we can verify the
# output as well.

test_expect_success setup '
	git commit --allow-empty -m A &&
	echo b >b &&
	git add b &&
	git commit -m B &&
	git checkout -b br1 HEAD^ &&
	echo c >c &&
	git add c &&
	git commit -m C &&
	git tag commit-C &&
	git merge -m D main &&
	git tag commit-D &&
	git checkout main &&
	git merge -m E commit-C &&
	git checkout -b br2 commit-C &&
	echo f >f &&
	git add f &&
	git commit -m F &&
	git checkout br1 &&
	git merge -m G br2 &&
	git checkout --orphan br3 &&
	git commit -m H
'

test_expect_success 'diff with one merge base' '
	git diff commit-D...br1 >tmp &&
	tail -n 1 tmp >actual &&
	echo +f >expect &&
	test_cmp expect actual
'

# The output (in tmp) can have +b or +c depending
# on which merge base (commit B or C) is picked.
# It should have one of those two, which comes out
# to seven lines.
test_expect_success 'diff with two merge bases' '
	git diff br1...main >tmp 2>err &&
	test_line_count = 7 tmp &&
	test_line_count = 1 err
'

test_expect_success 'diff with no merge bases' '
	test_must_fail git diff br2...br3 2>err &&
	test_i18ngrep "fatal: br2...br3: no merge base" err
'

test_expect_success 'diff with too many symmetric differences' '
	test_must_fail git diff br1...main br2...br3 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff with symmetric difference and extraneous arg' '
	test_must_fail git diff main br1...main 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff with two ranges' '
	test_must_fail git diff main br1..main br2..br3 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff with ranges and extra arg' '
	test_must_fail git diff main br1..main commit-D 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff --merge-base with no commits' '
	test_must_fail git diff --merge-base
'

test_expect_success 'diff --merge-base with three commits' '
	test_must_fail git diff --merge-base br1 br2 main 2>err &&
	test_i18ngrep "usage" err
'

for cmd in diff-index diff
do
	test_expect_success "$cmd --merge-base with one commit" '
		git checkout main &&
		git $cmd commit-C >expect &&
		git $cmd --merge-base br2 >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base with one commit and unstaged changes" '
		git checkout main &&
		test_when_finished git reset --hard &&
		echo unstaged >>c &&
		git $cmd commit-C >expect &&
		git $cmd --merge-base br2 >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base with one commit and staged and unstaged changes" '
		git checkout main &&
		test_when_finished git reset --hard &&
		echo staged >>c &&
		git add c &&
		echo unstaged >>c &&
		git $cmd commit-C >expect &&
		git $cmd --merge-base br2 >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base --cached with one commit and staged and unstaged changes" '
		git checkout main &&
		test_when_finished git reset --hard &&
		echo staged >>c &&
		git add c &&
		echo unstaged >>c &&
		git $cmd --cached commit-C >expect &&
		git $cmd --cached --merge-base br2 >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base with non-commit" '
		git checkout main &&
		test_must_fail git $cmd --merge-base main^{tree} 2>err &&
		test_i18ngrep "fatal: --merge-base only works with commits" err
	'

	test_expect_success "$cmd --merge-base with no merge bases and one commit" '
		git checkout main &&
		test_must_fail git $cmd --merge-base br3 2>err &&
		test_i18ngrep "fatal: no merge base found" err
	'

	test_expect_success "$cmd --merge-base with multiple merge bases and one commit" '
		git checkout main &&
		test_must_fail git $cmd --merge-base br1 2>err &&
		test_i18ngrep "fatal: multiple merge bases found" err
	'
done

for cmd in diff-tree diff
do
	test_expect_success "$cmd --merge-base with two commits" '
		git $cmd commit-C main >expect &&
		git $cmd --merge-base br2 main >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base commit and non-commit" '
		test_must_fail git $cmd --merge-base br2 main^{tree} 2>err &&
		test_i18ngrep "fatal: --merge-base only works with commits" err
	'

	test_expect_success "$cmd --merge-base with no merge bases and two commits" '
		test_must_fail git $cmd --merge-base br2 br3 2>err &&
		test_i18ngrep "fatal: no merge base found" err
	'

	test_expect_success "$cmd --merge-base with multiple merge bases and two commits" '
		test_must_fail git $cmd --merge-base main br1 2>err &&
		test_i18ngrep "fatal: multiple merge bases found" err
	'
done

test_expect_success 'diff-tree --merge-base with one commit' '
	test_must_fail git diff-tree --merge-base main 2>err &&
	test_i18ngrep "fatal: --merge-base only works with two commits" err
'

test_expect_success 'diff --merge-base with range' '
	test_must_fail git diff --merge-base br2..br3 2>err &&
	test_i18ngrep "fatal: --merge-base does not work with ranges" err
'

test_done
