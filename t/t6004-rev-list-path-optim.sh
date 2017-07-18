#!/bin/sh

test_description='git rev-list trivial path optimization test

   d/z1
   b0                             b1
   o------------------------*----o master
  /                        /
 o---------o----o----o----o side
 a0        c0   c1   a1   c2
 d/f0      d/f1
 d/z0

'

. ./test-lib.sh

test_expect_success setup '
	echo Hello >a &&
	mkdir d &&
	echo World >d/f &&
	echo World >d/z &&
	git add a d &&
	test_tick &&
	git commit -m "Initial commit" &&
	git rev-parse --verify HEAD &&
	git tag initial
'

test_expect_success path-optimization '
	test_tick &&
	commit=$(echo "Unchanged tree" | git commit-tree "HEAD^{tree}" -p HEAD) &&
	test $(git rev-list $commit | wc -l) = 2 &&
	test $(git rev-list $commit -- . | wc -l) = 1
'

test_expect_success 'further setup' '
	git checkout -b side &&
	echo Irrelevant >c &&
	echo Irrelevant >d/f &&
	git add c d/f &&
	test_tick &&
	git commit -m "Side makes an irrelevant commit" &&
	git tag side_c0 &&
	echo "More Irrelevancy" >c &&
	git add c &&
	test_tick &&
	git commit -m "Side makes another irrelevant commit" &&
	echo Bye >a &&
	git add a &&
	test_tick &&
	git commit -m "Side touches a" &&
	git tag side_a1 &&
	echo "Yet more Irrelevancy" >c &&
	git add c &&
	test_tick &&
	git commit -m "Side makes yet another irrelevant commit" &&
	git checkout master &&
	echo Another >b &&
	echo Munged >d/z &&
	git add b d/z &&
	test_tick &&
	git commit -m "Master touches b" &&
	git tag master_b0 &&
	git merge side &&
	echo Touched >b &&
	git add b &&
	test_tick &&
	git commit -m "Master touches b again"
'

test_expect_success 'path optimization 2' '
	git rev-parse side_a1 initial >expected &&
	git rev-list HEAD -- a >actual &&
	test_cmp expected actual
'

test_expect_success 'pathspec with leading path' '
	git rev-parse master^ master_b0 side_c0 initial >expected &&
	git rev-list HEAD -- d >actual &&
	test_cmp expected actual
'

test_expect_success 'pathspec with glob (1)' '
	git rev-parse master^ master_b0 side_c0 initial >expected &&
	git rev-list HEAD -- "d/*" >actual &&
	test_cmp expected actual
'

test_expect_success 'pathspec with glob (2)' '
	git rev-parse side_c0 initial >expected &&
	git rev-list HEAD -- "d/[a-m]*" >actual &&
	test_cmp expected actual
'

test_done
