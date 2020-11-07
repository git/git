#!/bin/sh

test_description='behavior of diff with symmetric-diff setups'

. ./test-lib.sh

# build these situations:
#  - normal merge with one merge base (br1...b2r);
#  - criss-cross merge ie 2 merge bases (br1...master);
#  - disjoint subgraph (orphan branch, br3...master).
#
#     B---E   <-- master
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
	git merge -m D master &&
	git tag commit-D &&
	git checkout master &&
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
	git diff br1...master >tmp 2>err &&
	test_line_count = 7 tmp &&
	test_line_count = 1 err
'

test_expect_success 'diff with no merge bases' '
	test_must_fail git diff br2...br3 >tmp 2>err &&
	test_i18ngrep "fatal: br2...br3: no merge base" err
'

test_expect_success 'diff with too many symmetric differences' '
	test_must_fail git diff br1...master br2...br3 >tmp 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff with symmetric difference and extraneous arg' '
	test_must_fail git diff master br1...master >tmp 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff with two ranges' '
	test_must_fail git diff master br1..master br2..br3 >tmp 2>err &&
	test_i18ngrep "usage" err
'

test_expect_success 'diff with ranges and extra arg' '
	test_must_fail git diff master br1..master commit-D >tmp 2>err &&
	test_i18ngrep "usage" err
'

test_done
