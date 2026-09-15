#!/bin/sh

test_description='merge-base with ancestor among merge-base candidates

Test that merge-base --all correctly handles cases where
multiple merge-base candidates exist and one is an ancestor
of another. The side-exhaustion optimization in
paint_down_to_common may exit before STALE propagation
removes the ancestor, but remove_redundant catches it.

Graph shape (parents are below children):

   A ----- X
   |\     /|
   | B---/ |
   |  \    |
   e2  \   f2
   |   |   |
   e1  d1  f1
    \  |  /
     \ | /
      \|/
       C

A and X are the two tips.
B and C are both reachable from A and X.
B reaches C through d1.
Only B should appear in merge-base --all output.
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup ancestor merge-base candidate' '
	test_commit C &&

	git checkout -b d-chain HEAD &&
	test_commit d1 &&
	test_commit B &&

	git checkout -b e-path C &&
	test_commit e1 &&
	test_commit e2 &&

	git checkout -b f-path C &&
	test_commit f1 &&
	test_commit f2 &&

	git checkout -b branch-A e-path &&
	test_merge A B &&

	git checkout -b branch-X f-path &&
	test_merge X B &&

	git commit-graph write --reachable
'

test_expect_success 'merge-base --all excludes ancestor candidate' '
	git rev-parse B >expected &&
	git merge-base --all A X >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-base (single) finds shallowest' '
	git rev-parse B >expected &&
	git merge-base A X >actual &&
	test_cmp expected actual
'

# Without commit-graph: generation numbers are INFINITY,
# side-exhaustion optimization does not fire.
test_expect_success 'merge-base --all without commit-graph' '
	rm -f .git/objects/info/commit-graph &&
	git rev-parse B >expected &&
	git merge-base --all A X >actual &&
	test_cmp expected actual
'

test_done
