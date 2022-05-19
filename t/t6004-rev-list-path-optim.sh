#!/bin/sh

test_description='but rev-list trivial path optimization test

   d/z1
   b0                             b1
   o------------------------*----o main
  /                        /
 o---------o----o----o----o side
 a0        c0   c1   a1   c2
 d/f0      d/f1
 d/z0

'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo Hello >a &&
	mkdir d &&
	echo World >d/f &&
	echo World >d/z &&
	but add a d &&
	test_tick &&
	but cummit -m "Initial cummit" &&
	but rev-parse --verify HEAD &&
	but tag initial
'

test_expect_success path-optimization '
	test_tick &&
	cummit=$(echo "Unchanged tree" | but cummit-tree "HEAD^{tree}" -p HEAD) &&
	test $(but rev-list $cummit | wc -l) = 2 &&
	test $(but rev-list $cummit -- . | wc -l) = 1
'

test_expect_success 'further setup' '
	but checkout -b side &&
	echo Irrelevant >c &&
	echo Irrelevant >d/f &&
	but add c d/f &&
	test_tick &&
	but cummit -m "Side makes an irrelevant cummit" &&
	but tag side_c0 &&
	echo "More Irrelevancy" >c &&
	but add c &&
	test_tick &&
	but cummit -m "Side makes another irrelevant cummit" &&
	echo Bye >a &&
	but add a &&
	test_tick &&
	but cummit -m "Side touches a" &&
	but tag side_a1 &&
	echo "Yet more Irrelevancy" >c &&
	but add c &&
	test_tick &&
	but cummit -m "Side makes yet another irrelevant cummit" &&
	but checkout main &&
	echo Another >b &&
	echo Munged >d/z &&
	but add b d/z &&
	test_tick &&
	but cummit -m "Main touches b" &&
	but tag main_b0 &&
	but merge side &&
	echo Touched >b &&
	but add b &&
	test_tick &&
	but cummit -m "Main touches b again"
'

test_expect_success 'path optimization 2' '
	but rev-parse side_a1 initial >expected &&
	but rev-list HEAD -- a >actual &&
	test_cmp expected actual
'

test_expect_success 'pathspec with leading path' '
	but rev-parse main^ main_b0 side_c0 initial >expected &&
	but rev-list HEAD -- d >actual &&
	test_cmp expected actual
'

test_expect_success 'pathspec with glob (1)' '
	but rev-parse main^ main_b0 side_c0 initial >expected &&
	but rev-list HEAD -- "d/*" >actual &&
	test_cmp expected actual
'

test_expect_success 'pathspec with glob (2)' '
	but rev-parse side_c0 initial >expected &&
	but rev-list HEAD -- "d/[a-m]*" >actual &&
	test_cmp expected actual
'

test_done
