#!/bin/sh

test_description='behavior of diff with symmetric-diff setups and --merge-base'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
# We put files into a few cummits so that we can verify the
# output as well.

test_expect_success setup '
	but cummit --allow-empty -m A &&
	echo b >b &&
	but add b &&
	but cummit -m B &&
	but checkout -b br1 HEAD^ &&
	echo c >c &&
	but add c &&
	but cummit -m C &&
	but tag CUMMIT-C &&
	but merge -m D main &&
	but tag CUMMIT-D &&
	but checkout main &&
	but merge -m E CUMMIT-C &&
	but checkout -b br2 CUMMIT-C &&
	echo f >f &&
	but add f &&
	but cummit -m F &&
	but checkout br1 &&
	but merge -m G br2 &&
	but checkout --orphan br3 &&
	but cummit -m H
'

test_expect_success 'diff with one merge base' '
	but diff CUMMIT-D...br1 >tmp &&
	tail -n 1 tmp >actual &&
	echo +f >expect &&
	test_cmp expect actual
'

# The output (in tmp) can have +b or +c depending
# on which merge base (cummit B or C) is picked.
# It should have one of those two, which comes out
# to seven lines.
test_expect_success 'diff with two merge bases' '
	but diff br1...main >tmp 2>err &&
	test_line_count = 7 tmp &&
	test_line_count = 1 err
'

test_expect_success 'diff with no merge bases' '
	test_must_fail but diff br2...br3 2>err &&
	test_i18ngrep "fatal: br2...br3: no merge base" err
'

test_expect_success 'diff with too many symmetric differences' '
	test_must_fail but diff br1...main br2...br3 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff with symmetric difference and extraneous arg' '
	test_must_fail but diff main br1...main 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff with two ranges' '
	test_must_fail but diff main br1..main br2..br3 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff with ranges and extra arg' '
	test_must_fail but diff main br1..main CUMMIT-D 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff --merge-base with no cummits' '
	test_must_fail but diff --merge-base
'

test_expect_success 'diff --merge-base with three cummits' '
	test_must_fail but diff --merge-base br1 br2 main 2>err &&
	test_i18ngrep "usage" err
'

for cmd in diff-index diff
do
	test_expect_success "$cmd --merge-base with one cummit" '
		but checkout main &&
		but $cmd CUMMIT-C >expect &&
		but $cmd --merge-base br2 >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base with one cummit and unstaged changes" '
		but checkout main &&
		test_when_finished but reset --hard &&
		echo unstaged >>c &&
		but $cmd CUMMIT-C >expect &&
		but $cmd --merge-base br2 >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base with one cummit and staged and unstaged changes" '
		but checkout main &&
		test_when_finished but reset --hard &&
		echo staged >>c &&
		but add c &&
		echo unstaged >>c &&
		but $cmd CUMMIT-C >expect &&
		but $cmd --merge-base br2 >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base --cached with one cummit and staged and unstaged changes" '
		but checkout main &&
		test_when_finished but reset --hard &&
		echo staged >>c &&
		but add c &&
		echo unstaged >>c &&
		but $cmd --cached CUMMIT-C >expect &&
		but $cmd --cached --merge-base br2 >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base with non-cummit" '
		but checkout main &&
		test_must_fail but $cmd --merge-base main^{tree} 2>err &&
		test_i18ngrep "fatal: --merge-base only works with cummits" err
	'

	test_expect_success "$cmd --merge-base with no merge bases and one cummit" '
		but checkout main &&
		test_must_fail but $cmd --merge-base br3 2>err &&
		test_i18ngrep "fatal: no merge base found" err
	'

	test_expect_success "$cmd --merge-base with multiple merge bases and one cummit" '
		but checkout main &&
		test_must_fail but $cmd --merge-base br1 2>err &&
		test_i18ngrep "fatal: multiple merge bases found" err
	'
done

for cmd in diff-tree diff
do
	test_expect_success "$cmd --merge-base with two cummits" '
		but $cmd CUMMIT-C main >expect &&
		but $cmd --merge-base br2 main >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --merge-base cummit and non-cummit" '
		test_must_fail but $cmd --merge-base br2 main^{tree} 2>err &&
		test_i18ngrep "fatal: --merge-base only works with cummits" err
	'

	test_expect_success "$cmd --merge-base with no merge bases and two cummits" '
		test_must_fail but $cmd --merge-base br2 br3 2>err &&
		test_i18ngrep "fatal: no merge base found" err
	'

	test_expect_success "$cmd --merge-base with multiple merge bases and two cummits" '
		test_must_fail but $cmd --merge-base main br1 2>err &&
		test_i18ngrep "fatal: multiple merge bases found" err
	'
done

test_expect_success 'diff-tree --merge-base with one cummit' '
	test_must_fail but diff-tree --merge-base main 2>err &&
	test_i18ngrep "fatal: --merge-base only works with two cummits" err
'

test_expect_success 'diff --merge-base with range' '
	test_must_fail but diff --merge-base br2..br3 2>err &&
	test_i18ngrep "fatal: --merge-base does not work with ranges" err
'

test_done
